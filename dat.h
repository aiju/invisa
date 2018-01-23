#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <assert.h>
#include "visa.h"

#define nil NULL
#define nelem(x) (sizeof(x)/sizeof(*(x)))

typedef struct Attr Attr;
typedef struct Session Session;
typedef struct OpTab OpTab;
typedef CRITICAL_SECTION Lock;
typedef struct RWLock RWLock;
typedef struct Rendez Rendez;
typedef struct Buf Buf;

struct Rendez {
	CONDITION_VARIABLE cv;
	Lock *l;
};

struct RWLock {
	Lock l;
	Rendez r;
	int rd, wr, wrwait;
};

struct Buf {
	ViUInt32 n, rd, wr;
	ViStatus end;
	ViInt16 unget;
	char b[1];
};

struct Session {
	enum {
		SESSNONE,
		SESSDRM,
	} type;
	ViSession id;
	const OpTab *tab;
	Lock l;
	Attr *attr;
	Buf *rdbuf, *wrbuf, *iordbuf;
	void *aux;
	Session *drmnext, *drmprev;
};

struct Attr {
	ViAttr attr;
	enum {
		ATTYPE = 0xf,
		ATINT8 = 0x0,
		ATINT16,
		ATINT32,
		ATINT64,
		ATPTR,
		ATSTRING,
		
		ATRO = 0x10,
	} flags;
	ViUInt64 val;
	char *str;
	ViStatus (*get)(Session *, Attr *, void *);
	ViStatus (*set)(Session *, Attr *, ViAttrState);
	Attr *next;
};

struct OpTab {
	int intf;
	ViStatus (*open)(Session *, char **, int, ViAccessMode, ViUInt32, Session **);
	ViStatus (*read)(Session *, void *, ViUInt32, ViUInt32 *);
	ViStatus (*rawread)(Session *, void *, ViUInt32, ViUInt32 *);
	ViStatus (*write)(Session *, void *, ViUInt32, ViUInt32 *, int);
	ViStatus (*assertTrigger)(Session *, ViUInt16);
	ViStatus (*readSTB)(Session *, ViUInt16 *);
	ViStatus (*clear)(Session *);
	void (*close)(Session *);
};

extern const OpTab *optabs[];

enum {
	IMPL_VERSION = 0x1337,
	MANF_ID = 0xBEEF,
	SPEC_VERSION = 0x500700,
	
	DEFBUF = 4096,
};
