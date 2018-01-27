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
typedef struct GpibIntfc GpibIntfc;
typedef struct GpibTab GpibTab;
typedef struct RsrcId RsrcId;

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
		
		ATBOOL = ATINT16,
		
		ATRO = 0x10,
		ATCALLSET = 0x20,
		ATIFUNDEF = 0x40,
	} flags;
	ViUInt64 val;
	char *str;
	ViStatus (*get)(Session *, Attr *, void *);
	ViStatus (*set)(Session *, Attr *, ViAttrState);
	Attr *next;
};

enum {
	CLASS_ANY = -1,
	CLASS_NONE = 0,
	CLASS_INSTR,
	CLASS_INTFC,
	CLASS_BACKPLANE,
	CLASS_MEMACC,
	CLASS_SERVANT,
	CLASS_SOCKET,
};

struct RsrcId {
	char *raw;
	char *f[16];
	int nf;
	int intf, class;
	int bnum;
};

struct OpTab {
	int intf, class;
	ViStatus (*open)(Session *, RsrcId *, ViAccessMode, ViUInt32, Session **);
	ViStatus (*read)(Session *, void *, ViUInt32, ViUInt32 *);
	ViStatus (*rawread)(Session *, void *, ViUInt32, ViUInt32 *);
	ViStatus (*write)(Session *, void *, ViUInt32, ViUInt32 *, int);
	ViStatus (*assertTrigger)(Session *, ViUInt16);
	ViStatus (*readSTB)(Session *, ViUInt16 *);
	ViStatus (*clear)(Session *);
	void (*close)(Session *);
	ViStatus (*gpibCommand)(Session *, void *, ViUInt32, ViUInt32 *);
	ViStatus (*gpibControlREN)(Session *, ViUInt16);
	ViStatus (*gpibControlATN)(Session *, ViUInt16);
	ViStatus (*gpibSendIFC)(Session *);
	ViStatus (*gpibPassControl)(Session *, ViUInt16, ViUInt16);
};

extern const OpTab *optabs[];

enum {
	IMPL_VERSION = 0x1337,
	MANF_ID = 0xBEEF,
	SPEC_VERSION = 0x500700,
	
	DEFBUF = 4096,
};

enum {
	GPIBIFTALK = 1,
	GPIBIFLISTEN = 2,
	GPIBIFSC = 4,
	GPIBIFCIC = 8,
	GPIBIFATN = 16,
};

enum {
	NOADDR = VI_NO_SEC_ADDR,
	MULTIADDR = 0xF000,
	UNKADDR = 0xFF00,
};

struct GpibIntfc {
	Lock l;
	int ref, bnum;
	int state;
	ViByte lastcmd;
	ViUInt16 talkpad, talksad, listenpad, listensad;
	const GpibTab *tab;
	void *aux;
	GpibIntfc *next, *prev;
};

extern GpibIntfc gpibifs;

struct GpibTab {
	int (*enumerate)(GpibIntfc **);
	int (*stillalive)(GpibIntfc *);
	void (*destroy)(GpibIntfc *);
	ViStatus (*upstate)(GpibIntfc *, int);
	ViStatus (*lines)(GpibIntfc *, ViUInt8 *, ViUInt8 *);
	ViStatus (*read)(GpibIntfc *, void *, ViUInt32, ViUInt32 *, int, int, ViUInt32);
	ViStatus (*write)(GpibIntfc *, void *, ViUInt32, ViUInt32 *, int, ViUInt32);
	ViStatus (*setatn)(GpibIntfc *, int, ViUInt32);
	ViStatus (*setren)(GpibIntfc *, int, ViUInt32);
	ViStatus (*sendifc)(GpibIntfc *, ViUInt32);
};

enum {
	MAX_MSG = 256,
	MAX_ATTR = 256,
};

enum {
	LINEEOI = 0x80,
	LINEATN = 0x40,
	LINESRQ = 0x20,
	LINEREN = 0x10,
	LINEIFC = 0x08,
	LINENRFD = 0x04,
	LINENDAC = 0x02,
	LINEDAV = 0x01
};
