#include "dat.h"
#include "fns.h"

extern void sessinit();

BOOL APIENTRY
DllMain(HANDLE hModule, DWORD reason, LPVOID lpReserved)
{
	WSADATA wsaData;

	switch(reason){
	case DLL_PROCESS_ATTACH:
		sessinit();
		if(WSAStartup(MAKEWORD(2,2), &wsaData) != 0)
			return FALSE;
		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}
