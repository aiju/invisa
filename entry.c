#include "dat.h"
#include "fns.h"
#include <libusb.h>

extern void sessinit();
extern void gpibinit();

libusb_context *usbctxt;

BOOL APIENTRY
DllMain(HANDLE hModule, DWORD reason, LPVOID lpReserved)
{
	WSADATA wsaData;

	switch(reason){
	case DLL_PROCESS_ATTACH:
		sessinit();
		gpibinit();
		if(WSAStartup(MAKEWORD(2,2), &wsaData) != 0)
			return FALSE;
		if(libusb_init(&usbctxt) != 0)
			usbctxt = nil;
		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}
