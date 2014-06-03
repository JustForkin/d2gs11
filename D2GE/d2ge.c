#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include "d2gelib/d2server.h"
#include "d2gs.h"
#include "d2ge.h"
#include "eventlog.h"
#include "callback.h"
#include "vars.h"
#include "client.h"

/* variables */
static D2GSINFO					gD2GSInfo;
static HANDLE					ghServerThread;
static SOCKET					trunk_s;

/*********************************************************************
 * Purpose: to patch the D2 Game Engine
 * Return: void
 *********************************************************************/
int D2GEStartup(void)
{
	HANDLE		hEvent;
	DWORD		dwThreadId;
	DWORD		dwWait;
	/* init GE thread */
	D2GEPatch();
	if (!D2GEThreadInit()) {
		D2GEEventLog("D2GEStartup", "Failed to Initialize Server");
		return FALSE;
	}

	/* create event for notification of GE startup */
	hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!hEvent) return FALSE;

	/* startup the server thread */
	ghServerThread = CreateThread(NULL, 0, D2GEThread, (LPVOID)hEvent, 0, &dwThreadId);
	if (!ghServerThread) {
		D2GEEventLog("D2GEStartup", "Can't CreateThread D2GEThread. Code: %lu", GetLastError());
		CloseHandle(hEvent);
		return FALSE;
	}
	dwWait = WaitForSingleObject(hEvent, D2GE_INIT_TIMEOUT);
	if (dwWait!=WAIT_OBJECT_0) {
		CloseHandle(hEvent);
		return FALSE;
	}

	CloseHandle(hEvent);

	return TRUE;

} /* End of D2GEStartup() */


/*********************************************************************
 * Purpose: to shutdown the D2 Game Engine
 * Return: TRUE(success) or FALSE(failed)
 *********************************************************************/
int D2GECleanup(void)
{
	D2GSEndAllGames();
	gD2GSInfo.bStop = TRUE;
	if (ghServerThread) {
		WaitForSingleObject(ghServerThread, D2GE_SHUT_TIMEOUT);
		CloseHandle(ghServerThread);
		ghServerThread = NULL;
	}

	return TRUE;

} /* End of D2GEShutdown() */


/*********************************************************************
 * Purpose: to initialize the D2 Game Engine thread
 * Return: TRUE(success) or FALSE(failed)
 *********************************************************************/
int D2GEThreadInit(void)
{
	if (!D2GSGetInterface()) {
		D2GEEventLog("D2GSThread", "Failed to Get Server Interface");
		return FALSE;
	}

	gD2GSInfo.szVersion				= D2GS_VERSION_STRING;
	gD2GSInfo.dwLibVersion			= D2GS_LIBRARY_VERSION;
	gD2GSInfo.bIsNT					= d2gsconf.enablentmode;
	gD2GSInfo.bEnablePatch			= d2gsconf.enablegepatch;
	gD2GSInfo.fpEventLog			= D2GEEventLog;
	gD2GSInfo.fpErrorHandle			= D2GSErrorHandle;
	gD2GSInfo.fpCallback			= EventCallbackTableInit();
	gD2GSInfo.bPreCache				= d2gsconf.enableprecachemode;
	gD2GSInfo.dwIdleSleep			= 1;
	gD2GSInfo.dwBusySleep			= 1;
	gD2GSInfo.dwMaxGame				= d2gsconf.gemaxgames;
	gD2GSInfo.dwReserved0			= 1200;
	memset(gD2GSInfo.dwReserved, 0, sizeof(DWORD) * 24);
	return TRUE;

} /* D2GEThreadInit() */


/*********************************************************************
 * Purpose: to set the GE interface
 * Return: TRUE(success) or FALSE(failed)
 *********************************************************************/
static BOOL D2GSGetInterface(void)
{
	LPD2GSINTERFACE		lpD2GSInterface;

	lpD2GSInterface = QueryInterface();
	if (!lpD2GSInterface) return FALSE;

	D2GSStart					= lpD2GSInterface->D2GSStart;
	D2GSSendDatabaseCharacter	= lpD2GSInterface->D2GSSendDatabaseCharacter;
	D2GSRemoveClientFromGame	= lpD2GSInterface->D2GSRemoveClientFromGame;
	D2GSNewEmptyGame			= lpD2GSInterface->D2GSNewEmptyGame;
	D2GSEndAllGames				= lpD2GSInterface->D2GSEndAllGames;
	D2GSSendClientChatMessage	= lpD2GSInterface->D2GSSendClientChatMessage;
	D2GSSetTickCount			= lpD2GSInterface->D2GSSetTickCount;
	D2GSSetACData				= lpD2GSInterface->D2GSSetACData;
	D2GSInitConfig				= lpD2GSInterface->D2GSInitConfig;
	D2GSLoadConfig				= lpD2GSInterface->D2GSLoadConfig;

	return TRUE;

} /* End of D2GSGetInterface() */


/*********************************************************************
 * Purpose: called by Game Engine when error occur
 * Return: TRUE(success) or FALSE(failed)
 *********************************************************************/
static DWORD __stdcall D2GSErrorHandle(void)
{
	D2GEEventLog("D2GSErrorHandle", "Error occur, exiting...\n\n");

#ifdef DEBUG_ON_CONSOLE
	printf("Press Any Key to Continue");
	_getch();
#endif

	ExitProcess(0);
	return 0;

} /* End of D2GSErrorHandle() */

void D2GELoadConfig(LPCSTR configfile){
	D2GSLoadConfig(configfile);
	D2GSInitConfig();
}

/*********************************************************************
 * Purpose: D2 GE main thread
 * Return: return value of the thread
 *********************************************************************/
DWORD WINAPI D2GEThread(LPVOID lpParameter)
{
	DWORD			dwThreadId;
	DWORD			dwRetval;
	HANDLE			hObjects[2];
	DWORD			dwExitCode;
	HANDLE			hEvent;
	int				i;

	hEvent = (HANDLE)lpParameter;
	if (!hEvent) return FALSE;

	gD2GSInfo.bStop = FALSE;
	hObjects[0] = CreateEvent(NULL, FALSE, FALSE, NULL);
	gD2GSInfo.hEventInited = hObjects[0];
	if (!hObjects[0]) {
		D2GEEventLog("D2GEThread", "Error in CreateEvent. Code: %lu", GetLastError());
		SetEvent(hEvent);
		return FALSE;
	}

	for(i = 1; i < 2; i ++)
	{
		hObjects[i] = CreateThread(NULL, 0, D2GSStart, &gD2GSInfo, 0, &dwThreadId);
		if (!hObjects[i]) {
			D2GEEventLog("D2GEThread", "Error Creating Server Thread. Code: %lu", GetLastError());
			CloseHandle(hObjects[0]);
			SetEvent(hEvent);
			return FALSE;
		} else {
			D2GEEventLog("D2GEThread", "Server Thread %lu Created", dwThreadId);
		}
	}
	dwRetval = WaitForMultipleObjects(NELEMS(hObjects), hObjects, FALSE, D2GE_INIT_TIMEOUT);

	if (dwRetval==WAIT_FAILED) {
		D2GEEventLog("D2GEThread", "Wait Server Thread Failed. Code: %lu", GetLastError());
		SetEvent(hEvent);
	} else if (dwRetval==WAIT_TIMEOUT) {
		D2GEEventLog("D2GEThread", "Game Server Thread Timedout");
		SetEvent(hEvent);
	} else if (dwRetval==WAIT_OBJECT_0 + 1) {
		GetExitCodeThread(hObjects[i], &dwExitCode);
		D2GEEventLog("D2GEThread", "Game Server Thread Exit with %d", dwExitCode); 
		SetEvent(hEvent);
	} else if (dwRetval==WAIT_OBJECT_0) {
		D2GSGetInterface();
		D2GEEventLog("D2GEThread", "Game Server Thread Start Successfully");
		SetEvent(hEvent);
	} else {
		D2GEEventLog("D2GEThread", "Wait Server Thread Returned %d", dwRetval);
		SetEvent(hEvent);
	}
	WaitForSingleObject(hObjects[1], INFINITE);
	CloseHandle(hObjects[0]);
	CloseHandle(hObjects[1]);
	return TRUE;

} /* End of D2GEThread */


int __stdcall D2GSBind(SOCKET s, const struct sockaddr *name, int namelen)
{
	WSASetLastError(0);
	return 0;
}

int __stdcall D2GSListen(SOCKET s, int backlog)
{
	WSASetLastError(0);
	return 0;
}

int __stdcall D2GSAccept(SOCKET s, struct sockaddr *addr, int *addrlen)
{
	int client = (int)get_client();
	getpeername((SOCKET)client, addr, addrlen);
	D2GEEventLog(__FUNCTION__, "Accepted client socket: 0x%x", client);
	return client;
}

int __stdcall D2GSSelectAccept(int nfds, fd_set *readfs, fd_set *writefds, fd_set *exceptfds, const struct timeval *timeout)
{
	int timeout_ms = timeout->tv_sec * 1000 + timeout->tv_usec; 
	return wait_client(timeout_ms);
}

//static int IOCPTimer = 0;

int __stdcall D2GSGetIOCP(HANDLE CompletionPort, LPDWORD lpNumberOfBytesTransferred, PULONG_PTR lpCompletionKey, LPOVERLAPPED *lpOverlapped, DWORD dwMilliseconds)
{
	return GetQueuedCompletionStatus(CompletionPort, lpNumberOfBytesTransferred, lpCompletionKey, lpOverlapped, dwMilliseconds);
}

int __stdcall D2GSSend(SOCKET s, const char *buf, int len, int flags)
{
	return send(s, buf, len ,flags);
}

int __stdcall NopSendPacket(DWORD unk1, DWORD ClientID, unsigned char *ThePacket, DWORD PacketLen)
{
	return 0;
}

static int IOCPEntry;

void D2GEPatch(){
	unsigned char pattern[3] = {0xC2, 0x0C, 0x00};
	unsigned char patch[3] = {0xC2, 0x14, 0x00};
	unsigned char pattern2[1] = {0xC3};
	unsigned char patch2[3] = {0xC2, 0x08, 0x00};
	unsigned char patch3[1] = {0xEB};
	unsigned char patch4 = 48 + get_id();
	unsigned char patch5[2] = {'E', 48 + get_id()};
	unsigned char patch6[5] = {0x90, 0x90, 0x90, 0x90, 0x90};
	unsigned char patch7[1] = {0xEB};
	unsigned char patch8[1] = {0x01};
	unsigned char patch9[2] = {0x90, 0xE9};
	DWORD fog, d2net, addr_my_func, target_addr, e8_arg, vp_old, ff15_arg;

	//hook 6FF70E69 in fog.dll RVA: +0x20E69
	//int __stdcall bind(SOCKET s, const struct sockaddr *name, int namelen)
	fog = LoadLibrary("fog.dll");
	d2net = LoadLibrary("d2net.dll");

	addr_my_func = D2GSBind;
	target_addr = fog + 0x20E69;
	e8_arg = addr_my_func - target_addr - 5;
	VirtualProtect(target_addr + 1, 4, PAGE_EXECUTE_READWRITE, &vp_old); 
	memcpy(target_addr + 1, &e8_arg, 4);
	VirtualProtect(target_addr + 1, 4, vp_old, &vp_old); 
	
	addr_my_func = D2GSListen;
	target_addr = fog + 0x20E86;
	e8_arg = addr_my_func - target_addr - 5;
	VirtualProtect(target_addr + 1, 4, PAGE_EXECUTE_READWRITE, &vp_old); 
	memcpy(target_addr + 1, &e8_arg, 4);
	VirtualProtect(target_addr + 1, 4, vp_old, &vp_old); 

	addr_my_func = D2GSAccept;
	target_addr = fog + 0x201BE;
	e8_arg = addr_my_func - target_addr - 5;
	VirtualProtect(target_addr + 1, 4, PAGE_EXECUTE_READWRITE, &vp_old); 
	memcpy(target_addr + 1, &e8_arg, 4);
	VirtualProtect(target_addr + 1, 4, vp_old, &vp_old); 
	
	addr_my_func = D2GSSelectAccept;
	target_addr = fog + 0x209BE;
	e8_arg = addr_my_func - target_addr - 5;
	VirtualProtect(target_addr + 1, 4, PAGE_EXECUTE_READWRITE, &vp_old); 
	memcpy(target_addr + 1, &e8_arg, 4);
	VirtualProtect(target_addr + 1, 4, vp_old, &vp_old); 
	
	addr_my_func = NopSendPacket;
	target_addr = d2net + 0x6505;
	e8_arg = addr_my_func - target_addr - 5;
	VirtualProtect(target_addr + 1, 4, PAGE_EXECUTE_READWRITE, &vp_old); 
	memcpy(target_addr + 1, &e8_arg, 4);
	VirtualProtect(target_addr + 1, 4, vp_old, &vp_old); 

	/*
	addr_my_func = &IOCPEntry;
	IOCPEntry = D2GSGetIOCP;
	target_addr = fog + 0x20550;
	ff15_arg = addr_my_func;
	VirtualProtect(target_addr + 2, 4, PAGE_EXECUTE_READWRITE, &vp_old); 
	memcpy(target_addr + 2, &ff15_arg, 4);
	VirtualProtect(target_addr + 2, 4, vp_old, &vp_old); 
///6ff7527c

	addr_my_func = D2GSSend;
	target_addr = fog + 0x2527C;
	ff15_arg = addr_my_func;
	VirtualProtect(target_addr, 4, PAGE_EXECUTE_READWRITE, &vp_old); 
	memcpy(target_addr, &ff15_arg, 4);
	VirtualProtect(target_addr, 4, vp_old, &vp_old); 
	*/

	//patch fog qserver

	//retn 8 at 6ff66b2d
	/*target_addr = fog + 0x16B2D; 
	VirtualProtect(target_addr, 1, PAGE_EXECUTE_READWRITE, &vp_old);
	memcpy(target_addr, patch3, 1);
	VirtualProtect(target_addr, 1, vp_old, &vp_old);*/

	target_addr = fog + 0x16C88; 
	VirtualProtect(target_addr, 5, PAGE_EXECUTE_READWRITE, &vp_old);
	memcpy(target_addr, patch6, 5);
	VirtualProtect(target_addr, 5, vp_old, &vp_old);

	//wow thats a big shot, bypass QSHackList()
	target_addr = fog + 0x16BDD; 
	VirtualProtect(target_addr, 2, PAGE_EXECUTE_READWRITE, &vp_old);
	memcpy(target_addr, patch9, 2);
	VirtualProtect(target_addr, 2, vp_old, &vp_old);

	//jmp to no result
	/*target_addr = fog + 0x168DB; 
	VirtualProtect(target_addr, 1, PAGE_EXECUTE_READWRITE, &vp_old);
	memcpy(target_addr, patch3, 1);
	VirtualProtect(target_addr, 1, vp_old, &vp_old);*/

	target_addr = fog + 0x202D6; 
	VirtualProtect(target_addr, 1, PAGE_EXECUTE_READWRITE, &vp_old);
	memcpy(target_addr, patch3, 1);
	VirtualProtect(target_addr, 1, vp_old, &vp_old);

	//patch d2ge hack
	searchAndPatch(FindPlayerToken, 0xFFFF, pattern, 3, patch, 3);
	searchAndPatch(CloseGame, 0xFFFF, pattern2, 1, patch2, 3);

	//patch D2GEVar filename
	target_addr = 0x68010CF6; 
	VirtualProtect(target_addr, 1, PAGE_READWRITE, &vp_old);
	memcpy(target_addr, &patch4, 1);
	VirtualProtect(target_addr, 1, vp_old, &vp_old);

	target_addr = 0x6800FD6B; 
	VirtualProtect(target_addr, 1, PAGE_READWRITE, &vp_old);
	memcpy(target_addr, &patch4, 1);
	VirtualProtect(target_addr, 1, vp_old, &vp_old);

	target_addr = 0x68010477; 
	VirtualProtect(target_addr, 2, PAGE_READWRITE, &vp_old);
	memcpy(target_addr, &patch5, 2);
	VirtualProtect(target_addr, 2, vp_old, &vp_old);

	target_addr = 0x680035FF; 
	VirtualProtect(target_addr, 1, PAGE_READWRITE, &vp_old);
	memcpy(target_addr, &patch7, 1);
	VirtualProtect(target_addr, 1, vp_old, &vp_old);

}

short get_trunk_port()
{
	struct sockaddr_in my_addr;
	int port;
	int namelen = sizeof(struct sockaddr_in);
	getsockname(trunk_s, &my_addr, &namelen);
	port = ntohs(my_addr.sin_port);
	D2GEEventLog(__FUNCTION__, "Server listen on port %d.", d2gsconf.listenaddr, port);
	return port;
}