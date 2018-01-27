#include "dat.h"
#include "fns.h"
#include <setjmp.h>
#include <ctype.h>
#include <math.h>

typedef struct Fmt Fmt;

struct Fmt {
	Session *p;
	char *strp;
	char *fmt;
	va_list va;
	jmp_buf errbuf;
	int lastrc, lastch;
	/* buffer only used for printing */
	char buf[16];
	int bufp;
	
	enum {
		FMTMINUS = 1<<0,
		FMTPLUS = 1<<1,
		FMTSPACE = 1<<2,
		FMTAT1 = 1<<3,
		FMTAT2 = 1<<4,
		FMTAT3 = 1<<5,
		FMTHEX = 1<<6,
		FMTOCT = 1<<7,
		FMTBIN = 1<<8,
		FMTWIDTH = 1<<9,
		FMTPREC = 1<<10,
		FMTARRAY = 1<<11,
		FMTOB = 1<<12,
		FMTOL = 1<<13,
		FMTh = 1<<14,
		FMTl = 1<<15,
		FMTll = 1<<16,
		FMTL = 1<<17,
		FMTz = 1<<18,
		FMTZ = 1<<19,
		FMTSTAR = 1<<20,
	} flags;
	
	/* printing */
	int width, prec, array;
	/* scanning */
	int *widthp, *arrayp;
};

static void
fmtflush(Fmt *f)
{
	int rc;

	if(f->strp != nil){
		memcpy(f->strp, f->buf, f->bufp);
		f->strp += f->bufp;
		*f->strp = 0;
	}else if(rc = bufwrite(f->p, f->buf, f->bufp, nil), rc < 0)
		longjmp(f->errbuf, rc);
	f->bufp = 0;
}

static inline void
fmtputc(Fmt *f, char c)
{
	f->buf[f->bufp++] = c;
	if(f->bufp == sizeof(f->buf))
		fmtflush(f);
}

static void
fmtputs(Fmt *f, char *c)
{
	while(*c != 0)
		fmtputc(f, *c++);
}

static void
eol(Fmt *f)
{
	int rc;

	if(f->strp != nil)
		longjmp(f->errbuf, VI_SUCCESS_TERM_CHAR);
	fmtputc(f, '\n');
	fmtflush(f);
	if(rc = bufwrflush(f->p, 1), rc < 0)
		longjmp(f->errbuf, rc);
}

static inline int /* lilu dallas */
multiset(int x)
{
	return x & x - 1;
}

static char *
prepfbuf(Fmt *f, char *fmtbuf)
{
	char *fp;

	fp = fmtbuf;
	*fp++ = '%';
	if((f->flags & FMTMINUS) != 0) *fp++ = '-';
	if((f->flags & FMTPLUS) != 0) *fp++ = '+';
	if((f->flags & FMTSPACE) != 0) *fp++ = ' ';
	if((f->flags & FMTWIDTH) != 0) fp += sprintf(fp, "%d", f->width);
	if((f->flags & FMTPREC) != 0) fp += sprintf(fp, ".%d", f->prec);
	return fp;
}

static void
flt1d(Fmt *f, char fmtch, double val)
{
	char buf[128];
	char fmtbuf[64];
	char *fp;

	fp = prepfbuf(f, fmtbuf);
	sprintf(fp, "%c", fmtch);
	snprintf(buf, sizeof(buf), fmtbuf, val);
	fmtputs(f, buf);
}

static void
flt1ld(Fmt *f, char fmtch, double val)
{
	char buf[128];
	char fmtbuf[64];
	char *fp;

	fp = prepfbuf(f, fmtbuf);
	sprintf(fp, "L%c", fmtch);
	snprintf(buf, sizeof(buf), fmtbuf, val);
	fmtputs(f, buf);
}

static void
fltfmt(Fmt *f, char fmtch)
{
	float *fp;
	double *dp;
	long double *ldp;
	int narr;

	if(multiset(f->flags & (FMTAT1|FMTAT2|FMTAT3)) || (f->flags & (FMTh|FMTz|FMTZ|FMTll|FMTOB|FMTOL|FMTBIN|FMTOCT|FMTHEX)) != 0)
		longjmp(f->errbuf, VI_ERROR_INV_FMT);
	if(fmtch != 'f' && (f->flags & (FMTAT1|FMTAT2|FMTAT3)) != 0)
		longjmp(f->errbuf, VI_ERROR_INV_FMT);
	if((f->flags & FMTAT1) != 0){
		f->flags |= FMTPREC;
		f->prec = 0;
	}
	if((f->flags & FMTAT3) != 0)
		fmtch = 'E';
	narr = f->array;
	switch(f->flags & (FMTl|FMTL|FMTARRAY)){
	default: case FMTl:
		flt1d(f, fmtch, va_arg(f->va, double));
		break;
	case FMTL:
		flt1ld(f, fmtch, va_arg(f->va, long double));
		break;
	case FMTARRAY:
		fp = va_arg(f->va, float *);
		while(narr--){
			flt1d(f, fmtch, *fp++);
			if(narr) fmtputc(f, ',');
		}
		break;
	case FMTl|FMTARRAY:
		dp = va_arg(f->va, double *);
		while(narr--){
			flt1d(f, fmtch, *dp++);
			if(narr) fmtputc(f, ',');
		}
		break;
	case FMTL|FMTARRAY:
		ldp = va_arg(f->va, long double *);
		while(narr--){
			flt1ld(f, fmtch, *ldp++);
			if(narr) fmtputc(f, ',');
		}
		break;
	}
}

static void
atcode(Fmt *f, ViUInt64 val)
{
	int b;
	char prefix;
	int n, z, sp, i;
	static const char dig[] = "0123456789ABCDEF";

	for(n = 0; n < 64 && val>>n != 0; n++)
		;
	switch(f->flags & (FMTBIN|FMTOCT|FMTHEX)){
	case FMTBIN: prefix = 'B'; b = 1; break;
	case FMTOCT: prefix = 'Q'; b = 3; break;
	default: prefix = 'H'; b = 4; break;
	}
	n = (n + b - 1) / b;
	if(n == 0) n = 1;
	z = n < f->prec ? f->prec - n : 0;
	sp = f->width - (n + z + 2);
	if(sp < 0) sp = 0;
	if((f->flags & FMTMINUS) == 0)
		while(sp--)
			fmtputc(f, ' ');
	fmtputc(f, '#');
	fmtputc(f, prefix);
	while(z--)
		fmtputc(f, '0');
	for(i = n-1; i >= 0; i--)
		fmtputc(f, dig[val >> i * b & (1 << b) - 1]);
	if((f->flags & FMTMINUS) != 0)
		while(sp--)
			fmtputc(f, ' ');	
}

static void
int1(Fmt *f, char fmtch, ViInt64 val)
{
	char buf[128];
	char fmtbuf[64];
	char *fp;
	
	if(fmtch != 'd' && fmtch != 'i' || (f->flags & (FMTBIN|FMTOCT|FMTHEX)) != 0)
		switch(f->flags & (FMTl|FMTll|FMTh)){
		case FMTl: val = (unsigned long)val; break;
		case FMTll: break;
		case FMTh: val = (unsigned short)val; break;
		default: val = (unsigned int)val; break;
		}
	if((f->flags & (FMTBIN|FMTOCT|FMTHEX)) != 0){
		atcode(f, val);
		return;
	}
	fp = prepfbuf(f, fmtbuf);
	sprintf(fp, "ll%c", fmtch);
	snprintf(buf, sizeof(buf), fmtbuf, val);
	fmtputs(f, buf);
}

static void
intfmt(Fmt *f, char fmtch)
{
	char *p;
	ViInt64 v;
	int i;

	if(multiset(f->flags & (FMTAT1|FMTAT2|FMTAT3|FMTBIN|FMTOCT|FMTHEX)) || (f->flags & (FMTz|FMTZ|FMTL|FMTOB|FMTOL)) != 0)
		longjmp(f->errbuf, VI_ERROR_INV_FMT);
	if(fmtch != 'd' && fmtch != 'i' && (f->flags & (FMTAT1|FMTAT2|FMTAT3|FMTBIN|FMTOCT|FMTHEX)) != 0)
		longjmp(f->errbuf, VI_ERROR_INV_FMT);
	if((f->flags & FMTARRAY) != 0){
		p = va_arg(f->va, void *);
		for(i = 0; i < f->array; i++){
			switch(f->flags & (FMTl|FMTll|FMTh)){
			case FMTl: v = *(long*)p; p += sizeof(long); break;
			case FMTll: v = *(long long*)p; p += sizeof(long long); break;
			case FMTh: v = *(short*)p; p += sizeof(short); break;
			default: v = *(int*)p; p += sizeof(int); break;
			}
			int1(f, fmtch, v);
			if(i != f->array - 1)
				fmtputc(f, ',');
		}
	}else{
		switch(f->flags & (FMTl|FMTll|FMTh)){
		case FMTl: v = va_arg(f->va, long); break;
		case FMTll: v = va_arg(f->va, long long); break;
		case FMTh: v = va_arg(f->va, int); break;
		default: v = va_arg(f->va, int); break;
		}
		int1(f, fmtch, v);
	}
}

static void
charfmt(Fmt *f, char fmtch)
{
	if(f->flags != 0)
		longjmp(f->errbuf, VI_ERROR_INV_FMT);
	fmtputc(f, va_arg(f->va, int));
}

static void
strfmt(Fmt *f, char fmtch)
{
	int nch, nsp;
	char *s;

	if((f->flags & ~(FMTMINUS|FMTWIDTH|FMTPREC)) != 0)
		longjmp(f->errbuf, VI_ERROR_INV_FMT);
	s = va_arg(f->va, char *);
	if(s == nil) s = "<nil>";
	nch = strlen(s);
	if((f->flags & FMTPREC) != 0 && nch > f->prec) nch = f->prec;
	nsp = f->width - nch;
	if(nsp < 0) nsp = 0;
	if((f->flags & FMTMINUS) == 0)
		while(nsp--)
			fmtputc(f, ' ');
	while(nch--)
		fmtputc(f, *s++);
	if((f->flags & FMTMINUS) != 0)
		while(nsp--)
			fmtputc(f, ' ');	
}

/* BUG: assumes IEEE 754 and that floating point endianness matches integer endianness */
static void
binfmt(Fmt *f, char fmtch)
{
	int n;
	ViUInt8 *cp;
	ViUInt16 *sp;
	ViUInt32 *lp;
	ViUInt64 *llp;
	char buf[16];

	if((f->flags & ~(FMTh|FMTl|FMTll|FMTz|FMTZ|FMTOB|FMTOL)) != FMTWIDTH)
		longjmp(f->errbuf, VI_ERROR_INV_FMT);
	if(fmtch != 'y' && (f->flags & (FMTOB|FMTOL)) != 0)
		longjmp(f->errbuf, VI_ERROR_INV_FMT);
	if(fmtch == 'b'){
		fmtputc(f, '#');
		n = f->width;
		switch(f->flags & ~FMTWIDTH){
		case FMTh: n *= 2; break;
		case FMTl: case FMTz: n *= 4; break;
		case FMTll: case FMTZ: n *= 8; break;
		}
		sprintf(buf, "%d", n);
		assert(strlen(buf) < 10);
		fmtputc(f, '0' + strlen(buf));
		fmtputs(f, buf);
	}else if(fmtch == 'B'){
		fmtputc(f, '#');
		fmtputc(f, '0');
	}
	n = f->width;
	switch(f->flags & ~(FMTWIDTH|FMTOB)){
	default:
	case FMTOL:
		cp = va_arg(f->va, void *);
		while(n--)
			fmtputc(f, *cp++);
		break;
	case FMTh:
		sp = va_arg(f->va, void *);
		while(n--){
			fmtputc(f, *sp >> 8);
			fmtputc(f, *sp++);
		}
		break;
	case FMTh|FMTOL:
		sp = va_arg(f->va, void *);
		while(n--){
			fmtputc(f, *sp);
			fmtputc(f, *sp++ >> 8);
		}
		break;
	case FMTl: case FMTz:
		lp = va_arg(f->va, void *);
		while(n--){
			fmtputc(f, *lp >> 24);
			fmtputc(f, *lp >> 16);
			fmtputc(f, *lp >> 8);
			fmtputc(f, *lp++);
		}
		break;
	case FMTl|FMTOL: case FMTz|FMTOL:
		lp = va_arg(f->va, void *);
		while(n--){
			fmtputc(f, *lp);
			fmtputc(f, *lp >> 8);
			fmtputc(f, *lp >> 16);
			fmtputc(f, *lp++ >> 24);
		}
		break;
	case FMTll: case FMTZ:
		llp = va_arg(f->va, void *);
		while(n--){
			fmtputc(f, *llp >> 56);
			fmtputc(f, *llp >> 48);
			fmtputc(f, *llp >> 40);
			fmtputc(f, *llp >> 32);
			fmtputc(f, *llp >> 24);
			fmtputc(f, *llp >> 16);
			fmtputc(f, *llp >> 8);
			fmtputc(f, *llp++);
		}
		break;
	case FMTll|FMTOL: case FMTZ|FMTOL:
		llp = va_arg(f->va, void *);
		while(n--){
			fmtputc(f, *llp);
			fmtputc(f, *llp >> 8);
			fmtputc(f, *llp >> 16);
			fmtputc(f, *llp >> 24);
			fmtputc(f, *llp >> 32);
			fmtputc(f, *llp >> 40);
			fmtputc(f, *llp >> 48);
			fmtputc(f, *llp++ >> 56);
		}
		break;
	}
	if(fmtch == 'B')
		eol(f);
}

const static void (*fmttab[128])(Fmt *, char) = {
	['d'] = intfmt, ['x'] = intfmt, ['X'] = intfmt, ['o'] = intfmt, ['u'] = intfmt, ['i'] = intfmt,
	['f'] = fltfmt, ['F'] = fltfmt, ['e'] = fltfmt, ['E'] = fltfmt, ['g'] = fltfmt, ['G'] = fltfmt,
	['c'] = charfmt,
	['s'] = strfmt,
	['b'] = binfmt, ['B'] = binfmt, ['y'] = binfmt,
};

static void
numericmod(Fmt *f)
{
	for(;; f->fmt++)
		switch(*f->fmt){
		case 0: longjmp(f->errbuf, VI_ERROR_INV_FMT);
		case '+': f->flags |= FMTPLUS; break;
		case '-': f->flags |= FMTMINUS; break;
		case ' ': f->flags |= FMTSPACE; break;
		case '@': 
			switch(*++f->fmt){
			case '1': f->flags |= FMTAT1; break;
			case '2': f->flags |= FMTAT2; break;
			case '3': f->flags |= FMTAT3; break;
			case 'H': f->flags |= FMTHEX; break;
			case 'Q': f->flags |= FMTOCT; break;
			case 'B': f->flags |= FMTBIN; break;
			default: longjmp(f->errbuf, VI_ERROR_INV_FMT);
			}
			break;
		default: return; 
		}
}

/* BUG: * for %b, %B and %y should may be long int, not int (standard is unclear) */
static int
optnum(Fmt *f, char prefix, int fl)
{
	if(prefix != 0){
		if(*f->fmt != prefix)
			return 0;
		f->fmt++;
	}
	if(!isdigit(*f->fmt) && *f->fmt != '*'){
		if(prefix != 0)
			longjmp(f->errbuf, VI_ERROR_INV_FMT);
		return 0;
	}
	f->flags |= fl;
	if(*f->fmt == '*'){
		f->fmt++;
		return va_arg(f->va, int);
	}
	return strtol(f->fmt, &f->fmt, 10);
}

static ViStatus
fmtgo(Fmt *f)
{
	int rc;
	char c;

	if(rc = setjmp(f->errbuf), rc != 0){
		fmtflush(f);
		return f->strp != nil && rc == VI_SUCCESS_TERM_CHAR ? VI_SUCCESS : rc;
	}
	while(c = *f->fmt++, c != 0){
		if(c == '\n'){
			eol(f);
			continue;
		}
		if(c != '%'){
			fmtputc(f, c);
			continue;
		}
		if(*f->fmt == '%'){
			fmtputc(f, c);
			f->fmt++;
			continue;
		}
		f->flags = 0;
		f->width = f->prec = f->array = 0;
		numericmod(f);
		f->width = optnum(f, 0, FMTWIDTH);
		if(f->width < 0){
			f->width = -f->width;
			f->flags |= FMTMINUS;
		}
		f->prec = optnum(f, '.', FMTPREC);
		if(f->prec < 0){
			f->prec = 0;
			f->flags &= ~FMTPREC;
		}
		f->array = optnum(f, ',', FMTARRAY);
		if(f->array < 0)
			f->array = 0;
		if(*f->fmt == '!'){
			if(strncmp(f->fmt, "!ob", 3) == 0)
				f->flags |= FMTOB;
			else if(strncmp(f->fmt, "!ol", 3) == 0)
				f->flags |= FMTOL;
			else
				return VI_ERROR_INV_FMT;
			f->fmt += 3;
		}
		switch(*f->fmt++){
		case 'l':
			if(*f->fmt == 'l'){
				f->flags |= FMTll;
				f->fmt++;
			}else
				f->flags |= FMTl;
			break;
		case 'L': f->flags |= FMTL; break;
		case 'h': f->flags |= FMTh; break;
		case 'z': f->flags |= FMTz; break;
		case 'Z': f->flags |= FMTZ; break;
		default: --f->fmt;
		}
		if(*f->fmt <= 0 || fmttab[(int)*f->fmt] == nil) return VI_ERROR_INV_FMT;
		fmttab[(int)*f->fmt](f, *f->fmt);
		f->fmt++;
	}
	fmtflush(f);
	return VI_SUCCESS;
}

static ViStatus
_viVPrintf(Session *p, ViConstString fmt, va_list va)
{
	Fmt f;
	int rc;
	
	memset(&f, 0, sizeof(f));
	f.p = p;
	if(f.p == nil) return VI_ERROR_INV_SESSION;
	if(f.p->tab->write == nil) return viend(f.p, VI_ERROR_NSUP_OPER);
	f.fmt = (char*)fmt;
	va_copy(f.va, va);
	rc = fmtgo(&f);
	va_end(f.va);
	if(attrval(p, VI_ATTR_WR_BUF_OPER_MODE, VI_FLUSH_WHEN_FULL) == VI_FLUSH_ON_ACCESS)
		bufwrflush(p, 0);
	return rc;
}

ViStatus EXPORT _VI_FUNC
viVPrintf(ViSession vi, ViConstString fmt, va_list va)
{
	Session *p;
	
	p = vibegin(vi, -1);
	if(p == nil) return VI_ERROR_INV_SESSION;
	return viend(p, _viVPrintf(p, fmt, va));
}

ViStatus EXPORT _VI_FUNC
viVSPrintf(ViSession vi, ViByte *buf, ViConstString fmt, va_list va)
{
	Fmt f;
	int rc;
	
	if(buf == nil)
		return VI_ERROR_USER_BUF;
	memset(&f, 0, sizeof(f));
	f.p = vibegin(vi, -1);
	if(f.p == nil) return VI_ERROR_INV_SESSION;
	f.fmt = (char*)fmt;
	f.strp = (char*)buf;
	va_copy(f.va, va);
	rc = fmtgo(&f);
	va_end(f.va);
	*f.strp = 0;
	return viend(f.p, rc);
}

ViStatus EXPORT _VI_FUNCC
viPrintf(ViSession vi, ViConstString fmt, ...)
{
	va_list va;
	Session *p;
	int rc;
	
	p = vibegin(vi, -1);
	if(p == nil) return VI_ERROR_INV_SESSION;
	va_start(va, fmt);
	rc = _viVPrintf(p, fmt, va);
	va_end(va);
	return viend(p, rc);
}

ViStatus EXPORT _VI_FUNCC
viSPrintf(ViSession vi, ViByte *buf, ViConstString fmt, ...)
{
	va_list va;
	int rc;
	
	va_start(va, fmt);
	rc = viVSPrintf(vi, buf, fmt, va);
	va_end(va);
	return rc;
}

static int
scangetc(Fmt *f)
{
	int rc;
	ViByte x;
	ViUInt32 n;

	if(f->strp != nil){
		if(*f->strp == 0)
			return f->lastch = -1;
		return f->lastch = (ViByte)*f->strp++;
	}
	if(f->lastrc != VI_SUCCESS_MAX_CNT) return -1;
	f->lastrc = rc = bufread(f->p, &x, 1, &n);
	if(n == 0) return f->lastch = -1;
	return f->lastch = x;
}

static void
scanungetc(Fmt *f)
{
	int rc;

	if(f->lastch < 0)
		return;
	if(f->strp != nil){
		f->strp--;
	}else if(rc = bufungetc(f->p, f->lastch, f->lastrc), rc < 0)
		longjmp(f->errbuf, rc);
	f->lastch = -1;
	f->lastrc = VI_SUCCESS_MAX_CNT;
}

static void
skipspace(Fmt *f)
{
	int c;
	
	while(c = scangetc(f), c >= 0 && isspace(c))
		;
	scanungetc(f);
}

static void
charscan(Fmt *f, char fmtch)
{
	int n, c;
	char *p;

	if((f->flags & ~(FMTSTAR|FMTWIDTH)) != 0)
		longjmp(f->errbuf, VI_ERROR_INV_FMT);
	n = 1;
	if((f->flags & FMTWIDTH) != 0)
		n = f->width;	
	if((f->flags & FMTSTAR) != 0)
		p = va_arg(f->va, char *);
	else
		p = nil;
	if(f->widthp != nil) *f->widthp = 0;
	while(n--){
		c = scangetc(f);
		if(c < 0) break;
		if(p != nil) *p++ = c;
		if(f->widthp != nil) (*f->widthp)++;
	}
}

static void
strscan(Fmt *f, char fmtch)
{
	int max;
	int i;
	int c;
	char *p;

	if((f->flags & ~(FMTSTAR|FMTWIDTH)) != 0)
		longjmp(f->errbuf, VI_ERROR_INV_FMT);
	if(f->widthp != nil) *f->widthp = 0;
	skipspace(f);
	if((f->flags & FMTSTAR) != 0)
		max = 0;
	else if((f->flags & FMTWIDTH) != 0)
		max = f->width;
	else
		max = 0x7fffffff;
	if((f->flags & FMTSTAR) != 0)
		p = nil;
	else
		p = va_arg(f->va, char *);
	if(max > 0) *p = 0;
	if(f->widthp != nil) *f->widthp = 0;
	for(i = 0; c = scangetc(f), c >= 0 && (!isspace(c) || fmtch != 's'); i++){
		if(i < max - 1){
			*p++ = c;
			*p = 0;
			if(f->widthp != nil) (*f->widthp)++;
		}
		if(fmtch == 'T' && c == '\n') break;
	}
	scanungetc(f);
}

static void
numscan(Fmt *f, char fmtch)
{
	int c;
	char *tp;
	ViInt64 val;
	double d;
	char buf[128];
	char *p;
	int cfmt, efmt;
	int narr;

	if(fmtch == 'd' && (f->flags & ~(FMTSTAR|FMTl|FMTll|FMTh|FMTARRAY)) != 0)
		longjmp(f->errbuf, VI_ERROR_INV_FMT);
	if(fmtch != 'd' && (f->flags & ~(FMTSTAR|FMTl|FMTL|FMTARRAY)) != 0)
		longjmp(f->errbuf, VI_ERROR_INV_FMT);
	if((f->flags & FMTSTAR) != 0)
		tp = nil;
	else
		tp = va_arg(f->va, void *);
	narr = 1;
	if((f->flags & FMTARRAY) != 0)
		narr = f->array;
	if(f->arrayp != nil)
		*f->arrayp = 0;
	if(narr == 0)
		return;
capo:
	skipspace(f);
	c = scangetc(f);
	if(c < 0) return;
	val = 0;
	if(c == '#'){
		c = scangetc(f);
		switch(c){
		case 'H': case 'h':
			while(c = scangetc(f), c >= 0 && isxdigit(c)){
				val *= 16;
				if(c >= 'a') val += c - 'a' + 10;
				else if(c >= 'A') val += c - 'A' + 10;
				else val += c - '0';
			}
			scanungetc(f);
			break;
		case 'Q': case 'q':
			while(c = scangetc(f), c >= '0' && c <= '7')
				val = val * 8 + c - '0';
			scanungetc(f);
			break;
		case 'B': case 'b':
			while(c = scangetc(f), c == '0' || c == '1')
				val = val * 2 + c - '0';
			scanungetc(f);
			break;
		default:
nope:			scanungetc(f);
			longjmp(f->errbuf, VI_SUCCESS_TERM_CHAR);
		}
		d = val;
		goto gotval;
	}
	p = buf;
	#define accept { if(p < buf + sizeof(buf) - 1) *p++ = c; c = scangetc(f); }
	if(c == '+' || c == '-') accept
	if(!isdigit(c)) goto nope;
	while(c >= 0 && isdigit(c)) accept
	if(cfmt = c == '.'){
		accept
		while(isdigit(c)) accept
	}
	if(efmt = c == 'e' || c == 'E'){
		accept
		if(c == '+' || c == '-') accept
		if(!isdigit(c)) goto nope;
		while(isdigit(c)) accept
	}
	#undef accept
	scanungetc(f);
	*p = 0;
	if((f->flags & FMTll) != 0 && !efmt && !cfmt){
		val = strtoll(buf, nil, 0);
		if(tp != nil){
			*(long long*)tp = val;
			tp += sizeof(long long);
		}
	}else{
		d = strtod(buf, nil);
		val = round(d);
	gotval:
		if(tp == nil)
			;
		else if(fmtch == 'd'){
			switch(f->flags & (FMTl|FMTh|FMTll)){
			case FMTh: *(short*)tp = val; tp += sizeof(short); break;
			default: *(int*)tp = val; tp += sizeof(int); break;
			case FMTl: *(long*)tp = val; tp += sizeof(long); break;
			case FMTll: *(long long*)tp = val; tp += sizeof(long long); break;
			}
		}else
			switch(f->flags & (FMTl|FMTL)){
			default: *(float*)tp = d; tp += sizeof(float); break;
			case FMTl: *(double*)tp = d; tp += sizeof(double); break;
			case FMTL: *(long double*)tp = d; tp += sizeof(long double); break;
			}
	}
	if(f->arrayp != nil)
		(*f->arrayp)++;
	if(--narr > 0){
		skipspace(f);
		if(scangetc(f) != ','){
			scanungetc(f);
			return;
		}
		goto capo;
	}
}

static void
binscan(Fmt *f, char fmtch)
{
	int c;
	ViByte buf[10];
	int cnt, max, i, elsz;
	char *tp;

	if((f->flags & ~(FMTSTAR|FMTh|FMTl|FMTll|FMTz|FMTZ)) != FMTWIDTH)
		longjmp(f->errbuf, VI_ERROR_INV_FMT);
	skipspace(f);
	c = scangetc(f);
	if(c != '#'){
nope:
		scanungetc(f);
		longjmp(f->errbuf, VI_SUCCESS_TERM_CHAR);
	}
	c = scangetc(f);
	if(c < 0 || !isdigit(c)) goto nope;
	if(c == '0')
		cnt = -1;
	else{
		cnt = c - '0';
		for(i = 0; i < cnt; i++){
			c = scangetc(f);
			if(c < 0 || !isdigit(c)) goto nope;
			buf[i] = c;
		}
		buf[i] = 0;
		cnt = atoi((char*)buf);
	}
	if((f->flags & FMTSTAR) != 0){
		max = 0;
		tp = nil;
	}else{
		max = f->width;
		tp = va_arg(f->va, void *);
	}
	if(f->widthp != nil) *f->widthp = 0;
	switch(f->flags & (FMTh|FMTl|FMTll|FMTz|FMTZ)){
	default: elsz = 1; break;
	case FMTh: elsz = 2; break;
	case FMTl: case FMTz: elsz = 4; break;
	case FMTll: case FMTZ: elsz = 8; break;
	}
	while(cnt != 0){
		for(i = 0; i < elsz; i++)
			if(cnt < 0 || cnt > 0 && (--cnt, 1)){
				c = scangetc(f);
				if(c < 0){
					if(i == 0)
						return;
					cnt = 0;
				}
				if(cnt < 0 && c == '\n'){
					c = scangetc(f);
					scanungetc(f);
					if(c < 0){
						if(i == 0)
							return;
						cnt = 0;
					}else
						c = '\n';
				}
				buf[i] = c;
			}else
				buf[i] = 0;
		if(tp != nil && max > 0){
			switch(f->flags & (FMTh|FMTl|FMTll|FMTz|FMTZ)){
			default: *tp = buf[0]; break;
			case FMTh: *(ViUInt16*)tp = buf[0] << 8 | buf[1]; break;
			case FMTl: case FMTz: *(ViUInt32*)tp = buf[0] << 24 | buf[1] << 16 | buf[2] << 8 | buf[3]; break;
			case FMTll: case FMTZ: *(ViUInt64*)tp = (ViUInt64)buf[0] << 56 | (ViUInt64)buf[1] << 48 | (ViUInt64)buf[2] << 40 | (ViUInt64)buf[3] << 32 | (ViUInt64)buf[4] << 24 | (ViUInt64)buf[5] << 16 | (ViUInt64)buf[6] << 8 | buf[7];
			}
			tp += elsz;
			max--;
			if(f->widthp != nil) (*f->widthp)++;
		}
	}
}

static void
rawscan(Fmt *f, char fmtch)
{
	int c;
	ViByte buf[10];
	int cnt, max, i, elsz;
	char *tp;

	if((f->flags & ~(FMTSTAR|FMTh|FMTl|FMTll|FMTOB|FMTOL)) != FMTWIDTH)
		longjmp(f->errbuf, VI_ERROR_INV_FMT);
	if((f->flags & FMTSTAR) != 0){
		max = 0;
		tp = nil;
	}else{
		max = f->width;
		tp = va_arg(f->va, void *);
	}
	if(f->widthp != nil) *f->widthp = 0;
	switch(f->flags & (FMTh|FMTl|FMTll|FMTz|FMTZ)){
	default: elsz = 1; break;
	case FMTh: elsz = 2; break;
	case FMTl: case FMTz: elsz = 4; break;
	case FMTll: case FMTZ: elsz = 8; break;
	}
	cnt = f->width * elsz;
	while(cnt != 0){
		for(i = 0; i < elsz; i++)
			if(cnt > 0){
				cnt--;
				c = scangetc(f);
				if(c < 0){
					cnt = 0;
					if(i == 0)
						return;
				}
				buf[i] = c;
			}else
				buf[i] = 0;
		if(tp != nil && max > 0){
			switch(f->flags & (FMTh|FMTl|FMTll|FMTOL)){
			default: *tp = buf[0]; break;
			case FMTh: *(ViUInt16*)tp = buf[0] << 8 | buf[1]; break;
			case FMTl: *(ViUInt32*)tp = buf[0] << 24 | buf[1] << 16 | buf[2] << 8 | buf[3]; break;
			case FMTll: *(ViUInt64*)tp = (ViUInt64)buf[0] << 56 | (ViUInt64)buf[1] << 48 | (ViUInt64)buf[2] << 40 | (ViUInt64)buf[3] << 32 | (ViUInt64)buf[4] << 24 | (ViUInt64)buf[5] << 16 | (ViUInt64)buf[6] << 8 | buf[7]; break;
			case FMTh|FMTOL: *(ViUInt16*)tp = buf[0] | buf[1] << 8; break;
			case FMTl|FMTOL: *(ViUInt32*)tp = buf[0] | buf[1] << 8 | buf[2] << 16 | buf[3] << 24; break;
			case FMTll|FMTOL: *(ViUInt64*)tp = (ViUInt64)buf[0] | (ViUInt64)buf[1] << 8 | (ViUInt64)buf[2] << 16 | (ViUInt64)buf[3] << 24 | (ViUInt64)buf[4] << 32 | (ViUInt64)buf[5] << 40 | (ViUInt64)buf[6] << 48 | (ViUInt64)buf[7] << 56; break;
			}
			tp += elsz;
			max--;
			if(f->widthp != nil) (*f->widthp)++;
		}
	}

}

static void (*scantab[128])(Fmt *, char) = {
	['c'] = charscan,
	['s'] = strscan,
	['t'] = strscan,
	['T'] = strscan,
	['d'] = numscan, ['f'] = numscan, ['e'] = numscan, ['E'] = numscan, ['g'] = numscan, ['G'] = numscan,
	['b'] = binscan,
	['y'] = rawscan,
};

static ViStatus
scango(Fmt *f)
{
	int rc;
	char c;

	if(rc = setjmp(f->errbuf), rc != 0)
		return rc == VI_SUCCESS_TERM_CHAR ? VI_SUCCESS : rc;
	while(c = *f->fmt++, c != 0){
		if(isspace(c)){
			skipspace(f);
			while(isspace(*f->fmt))
				f->fmt++;
			continue;
		}
		if(c != '%'){
			if(scangetc(f) != c){
				scanungetc(f);
				break;
			}
			continue;
		}
		if(*f->fmt == '%'){
			if(scangetc(f) != '%'){
				scanungetc(f);
				break;
			}
			f->fmt++;
			continue;
		}
		if(*f->fmt == '*'){
			f->flags |= FMTSTAR;
			f->fmt++;
		}
		f->flags = 0;
		f->width = f->array = 0;
		f->widthp = f->arrayp = nil;
		if(isdigit(*f->fmt)){
			f->flags |= FMTWIDTH;
			f->width = strtol(f->fmt, &f->fmt, 10);
		}else if(*f->fmt == '#'){
			f->widthp = va_arg(f->va, int *);
			f->width = *f->widthp;
			f->flags |= FMTWIDTH;
			if(f->width < 0) f->width = 0;
			f->fmt++;
		}
		if(*f->fmt == ','){
			f->fmt++;
			if(isdigit(*f->fmt)){
				f->flags |= FMTARRAY;
				f->array = strtol(f->fmt, &f->fmt, 10);
			}else if(*f->fmt == '#'){
				f->arrayp = va_arg(f->va, int *);
				f->array = *f->arrayp;
				if(f->array < 0) f->array = 0;
				f->flags |= FMTARRAY;
				f->fmt++;
			}else
				return VI_ERROR_INV_FMT;
		}
		if(*f->fmt == '!'){
			if(strncmp(f->fmt, "!ob", 3) == 0)
				f->flags |= FMTOB;
			else if(strncmp(f->fmt, "!ol", 3) == 0)
				f->flags |= FMTOL;
			else
				return VI_ERROR_INV_FMT;
			f->fmt += 3;
		}
		switch(*f->fmt++){
		case 'l':
			if(*f->fmt == 'l'){
				f->flags |= FMTll;
				f->fmt++;
			}else
				f->flags |= FMTl;
			break;
		case 'L': f->flags |= FMTL; break;
		case 'h': f->flags |= FMTh; break;
		case 'z': f->flags |= FMTz; break;
		case 'Z': f->flags |= FMTZ; break;
		default: --f->fmt;
		}
		if(*f->fmt < 0 || scantab[(int)*f->fmt] == nil) return VI_ERROR_INV_FMT;
		scantab[(int)*f->fmt](f, *f->fmt);
		f->fmt++;
	}
	if(f->lastrc < 0)
		return f->lastrc;
	return VI_SUCCESS;
}

static ViStatus
_viVScanf(Session *p, ViConstString fmt, va_list va)
{
	Fmt f;
	int rc;
	
	if(p->tab->read == nil) return VI_ERROR_NSUP_OPER;
	memset(&f, 0, sizeof(f));
	f.lastrc = VI_SUCCESS_MAX_CNT;
	f.p = p;
	f.fmt = (char*)fmt;
	va_copy(f.va, va);
	rc = scango(&f);
	va_end(f.va);
	if(attrval(p, VI_ATTR_RD_BUF_OPER_MODE, VI_FLUSH_WHEN_FULL) == VI_FLUSH_ON_ACCESS)
		bufrdflush(p);
	return rc;
}

ViStatus EXPORT _VI_FUNC
viVScanf(ViSession vi, ViConstString fmt, va_list va)
{
	Session *p;
	
	p = vibegin(vi, -1);
	if(p == nil) return VI_ERROR_INV_SESSION;
	return viend(p, _viVScanf(p, fmt, va));
}

ViStatus EXPORT _VI_FUNC
viVSScanf(ViSession vi, const ViByte *buf, ViConstString fmt, va_list va)
{
	Fmt f;
	int rc;
	
	if(buf == nil)
		return VI_ERROR_USER_BUF;
	memset(&f, 0, sizeof(f));
	f.p = vibegin(vi, -1);
	if(f.p == nil) return VI_ERROR_INV_SESSION;
	f.fmt = (char*)fmt;
	f.strp = (char*)buf;
	va_copy(f.va, va);
	rc = scango(&f);
	va_end(f.va);
	return viend(f.p, rc);
}

ViStatus EXPORT _VI_FUNCC
viScanf(ViSession vi, ViConstString fmt, ...)
{
	va_list va;
	int rc;
	Session *p;
	
	p = vibegin(vi, -1);
	if(p == nil) return VI_ERROR_INV_SESSION;
	va_start(va, fmt);
	rc = _viVScanf(p, fmt, va);
	va_end(va);
	return viend(p, rc);
}

ViStatus EXPORT _VI_FUNCC
viSScanf(ViSession vi, ViConstBuf buf, ViConstString fmt, ...)
{
	va_list va;
	int rc;
	
	va_start(va, fmt);
	rc = viVSScanf(vi, buf, fmt, va);
	va_end(va);
	return rc;
}

ViStatus EXPORT _VI_FUNC
viVQueryf(ViSession vi, ViConstString writeFmt, ViConstString readFmt, va_list va)
{
	Session *p;
	int rc;
	
	p = vibegin(vi, -1);
	if(p == nil) return VI_ERROR_INV_SESSION;
	bufrdflush(p);
	rc = _viVPrintf(p, writeFmt, va);
	if(rc >= 0)
		rc = bufwrflush(p, 1);
	if(rc >= 0)
		rc = _viVScanf(p, readFmt, va);
	return viend(p, rc);
}

ViStatus EXPORT _VI_FUNCC
viQueryf(ViSession vi, ViConstString writeFmt, ViConstString readFmt, ...)
{
	va_list va;
	int rc;
	
	va_start(va, readFmt);
	rc = viVQueryf(vi, writeFmt, readFmt, va);
	va_end(va);
	return rc;
}
