#include "visa.h"
#include <stdio.h>
#include <stdlib.h>

static void
errcheck(ViStatus status, char *doop)
{
	char buf[256];

	viStatusDesc(0, status, buf);
	printf("%s: %s\n", doop, buf);
	if(status<0)	exit(0);
}

#define CHECK(x) errcheck(x, #x)

int main() 
{
	ViSession drm, vi;
	char test[] = " #15ABCDE";
	char buf[8];
	int m;

	CHECK(viOpenDefaultRM(&drm));
	CHECK(viOpen(drm, "TCPIP::localhost::1337::SOCKET", 0, 0, &vi));
	CHECK(viSetAttribute(vi, VI_ATTR_TERMCHAR, '\n'));
	CHECK(viSetAttribute(vi, VI_ATTR_TERMCHAR_EN, 1));
	m = sizeof(buf);
	CHECK(viScanf(vi, "%#s %*s\n", &m, buf));
	printf("%d %s\n", m, buf);
}
