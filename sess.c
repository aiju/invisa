#include "dat.h"
#include "fns.h"

Session **sesstab;
int nsesstab;
RWLock sesslock;
enum { SESSBLOCK = 64 };

extern const OpTab tcpiptab;
const OpTab *optabs[] = {
	&tcpiptab,
	nil
};

void
sessinit(void)
{
	newrwlock(&sesslock);
}

Session *
vibegin(ViSession fd, int stype)
{
	Session *p;

	rlock(&sesslock);
	if(fd >= nsesstab)
		p = nil;
	else
		p = sesstab[fd];
	runlock(&sesslock);
	if(p == nil || stype >= 0 && p->type != stype)
		return nil;
	lock(&p->l);
	return p;
}

ViStatus
viend(Session *p, ViStatus rc)
{
	unlock(&p->l);
	return rc;
}

Attr *
getattr(Session *p, ViAttr a, int mk)
{
	Attr **q;
	
	for(q = &p->attr; (*q) != nil; q = &(*q)->next)
		if((*q)->attr == a)
			return *q;
	if(!mk)
		return nil;
	*q = calloc(sizeof(Attr), 1);
	if(*q == nil)
		return nil;
	(*q)->attr = a;
	return *q;
}

ViAttrState
attrval(Session *p, ViAttr attr, ViAttrState def)
{
	Attr *a;
	
	a = getattr(p, attr, 0);
	if(a == nil) return def;
	return a->val;
}

Attr *
newattrx(Session *p, ViAttr attr, int flags, ViAttrState in, ViStatus (*get)(Session *, Attr *, void *), ViStatus (*set)(Session *, Attr *, ViAttrState))
{
	Attr *a;
	
	a = getattr(p, attr, 1);
	if(a == nil)
		return nil;
	a->flags = flags;
	a->get = get;
	a->set = set;
	switch(flags & ATTYPE){
	case ATINT8: a->val = (ViUInt8) in; break;
	case ATINT16: a->val = (ViUInt16) in; break;
	case ATINT32: a->val = (ViUInt32) in; break;
	case ATINT64: a->val = (ViUInt64) in; break;
	case ATPTR: a->val = (uintptr_t) in; break;
	default: assert(0); return nil;
	}
	return a;
}

Attr *
newattri(Session *p, ViAttr attr, int flags, ViAttrState in)
{
	return newattrx(p, attr, flags, in, nil, nil);
}

Attr *
newattrs(Session *p, ViAttr attr, int flags, char *in)
{
	Attr *a;
	
	a = getattr(p, attr, 1);
	if(a == nil)
		return nil;
	a->flags = flags | ATSTRING | ATRO;
	free(a->str);
	a->str = strdup(in);
	return a;
}

#ifdef VI_ATTR_USER_DATA_64
static ViStatus
user32get(Session *p, Attr *a, void *v)
{
	Attr *b;
	
	b = getattr(p, VI_ATTR_USER_DATA_64, 0);
	if(b == nil) return VI_ERROR_NSUP_ATTR;
	*(ViUInt32*)v = b->val;
	return VI_SUCCESS;
}
static ViStatus
user32set(Session *p, Attr *a, ViAttrState v)
{
	Attr *b;
	
	b = getattr(p, VI_ATTR_USER_DATA_64, 0);
	if(b == nil) return VI_ERROR_NSUP_ATTR;
	b->val = (ViUInt32) v;
	return VI_SUCCESS;
}
#endif

ViStatus
newsession(ViPSession fp, Session **rp, Session *drm)
{
	int i;
	Session *p;
	Session **n;

	p = calloc(sizeof(Session), 1);
	if(p == nil)
		return VI_ERROR_ALLOC;
	wlock(&sesslock);
	for(i = 0; i < nsesstab; i++)
		if(i != 0 && sesstab[i] == nil)
			break;
	if(i == nsesstab){
		n = realloc(sesstab, sizeof(Session *) * (nsesstab + SESSBLOCK));
		if(n == nil){
			free(p);
			wunlock(&sesslock);
			return VI_ERROR_ALLOC;
		}
		sesstab = n;
		memset(sesstab + nsesstab, 0, SESSBLOCK * sizeof(Session *));
		i = nsesstab;
		if(i == 0) i = 1;
		nsesstab += SESSBLOCK;
	}
	sesstab[i] = p;
	wunlock(&sesslock);
	newlock(&p->l);
	p->id = i;
	if(fp != nil) *fp = i;
	if(rp != nil) *rp = p;
	if(drm != nil){
		p->drmnext = drm;
		p->drmprev = drm->drmprev;
		p->drmnext->drmprev = p;
		p->drmprev->drmnext = p;
	}else
		p->drmnext = p->drmprev = p;
	newattri(p, VI_ATTR_RSRC_IMPL_VERSION, ATINT32|ATRO, IMPL_VERSION);
	newattri(p, VI_ATTR_RSRC_LOCK_STATE, ATINT32|ATRO, VI_NO_LOCK);
	newattri(p, VI_ATTR_RSRC_MANF_ID, ATINT16|ATRO, MANF_ID);
	newattrs(p, VI_ATTR_RSRC_MANF_NAME, 0, "hjvisa");
	newattrs(p, VI_ATTR_RSRC_NAME, 0, "");
	newattri(p, VI_ATTR_RSRC_SPEC_VERSION, ATINT32|ATRO, SPEC_VERSION);
	newattri(p, VI_ATTR_RM_SESSION, ATINT32|ATRO, drm != nil ? drm->id : VI_NULL);
	newattri(p, VI_ATTR_MAX_QUEUE_LENGTH, ATINT32, 1);
	newattrs(p, VI_ATTR_RSRC_CLASS, 0, "");
#ifdef VI_ATTR_USER_DATA_64
	newattri(p, VI_ATTR_USER_DATA_64, ATINT64, 0);
	newattrx(p, VI_ATTR_USER_DATA_32, ATINT32, 0, user32get, user32set);
#else
	newattri(p, VI_ATTR_USER_DATA_32, ATINT32, 0);
#endif
	return VI_SUCCESS;
}

int
rsrcsplit(char *p, char **f, int nf)
{
	int n;
	
	assert(nf > 0);
	n = 0;
	f[n++] = p;
	for(; *p != 0; p++){
		if(p[0] == ':' && p[1] == ':'){
			p[0] = 0;
			if(n < nf)
				f[n++] = &p[2];
			p++;
		}else if(p[0] == '[')
			while(*p != 0 && *p != ']')
				p++;
	}
	return n;
}

static const struct intfdata {
	char *name;
	int n;
} interfaces [] = {
	{"VXI", VI_INTF_VXI},
	{"GPIB", VI_INTF_GPIB},
	{"GPIB-VXI", VI_INTF_GPIB_VXI},
	{"ASRL", VI_INTF_ASRL},
	{"TCPIP", VI_INTF_TCPIP},
	{"USB", VI_INTF_USB},
	{"PXI", VI_INTF_PXI},
	{nil, 0},
};

static const char *classlist[] = {"INSTR", "INTFC", "BACKPLANE", "MEMACC", "SERVANT", "SOCKET", nil};

static ViStatus
rsrcparse(char *buf, char **f, int *nf, ViUInt16* ptype, ViUInt16 *pnum, ViString class)
{
	const struct intfdata *ip;
	const char **pp;
	char *p;
	int n, brd;
	
	*nf = n = rsrcsplit(buf, f, *nf);
	if(n < 2) return VI_ERROR_INV_RSRC_NAME;
	for(ip = interfaces; ip->name != nil; ip++)
		if(strncasecmp(f[0], ip->name, strlen(ip->name)) == 0)
			break;
	if(ip->name == nil)
		return VI_ERROR_INV_RSRC_NAME;
	p = f[0] + strlen(ip->name);
	if(*p == 0)
		brd = 0;
	else{
		brd = strtol(p, &p, 10);
		if(*p != 0)
			return VI_ERROR_INV_RSRC_NAME;
	}
	for(pp = classlist; *pp != nil; pp++)
		if(strcasecmp(f[n-1], *pp) == 0)
			break;
	if(ptype != nil) *ptype = ip->n;
	if(pnum != nil) *pnum = brd;
	if(class != nil){
		if(*pp == nil)
			strcpy(class, "INSTR");
		else
			strcpy(class, *pp);
	}
	return VI_SUCCESS;
}

ViStatus _VI_FUNC EXPORT
viParseRsrc(ViSession sesn, ViConstRsrc name, ViUInt16* ptype, ViUInt16 *pnum)
{
	return viParseRsrcEx(sesn, name, ptype, pnum, nil, nil, nil);
}


ViStatus _VI_FUNC EXPORT
viParseRsrcEx(ViSession sesn, ViConstRsrc name, ViUInt16* ptype, ViUInt16 *pnum, ViString class, ViString expa, ViString alias)
{
	char *buf;
	char *f[10];
	Session *p;
	int rc, nf;

	p = vibegin(sesn, SESSDRM);
	if(p == nil) return VI_ERROR_INV_SESSION;
	buf = strdup(name);
	if(buf == nil) return viend(p, VI_ERROR_ALLOC);
	nf = nelem(f);
	rc = rsrcparse(buf, f, &nf, ptype, pnum, class);
	if(rc >= 0){
		if(expa != nil) strcpy(expa, name);
		if(alias != nil) *alias = 0;
	}
	free(buf);
	return viend(p, rc);
}

ViStatus _VI_FUNC EXPORT
viOpen(ViSession sesn, ViConstRsrc name, ViAccessMode mode, ViUInt32 timeout, ViPSession vi)
{
	Session *drm, *p;
	const OpTab **tab;
	char *buf, *f[10];
	char class[20];
	ViUInt16 ptype, bnum;
	int rc, nf;
	
	drm = vibegin(sesn, SESSDRM);
	if(drm == nil) return VI_ERROR_INV_SESSION;
	buf = strdup(name);
	if(buf == nil) return VI_ERROR_ALLOC;
	nf = nelem(f);
	rc = rsrcparse(buf, f, &nf, &ptype, &bnum, class);
	if(rc < 0){
		free(buf);
		return viend(drm, rc);
	}
	rc = VI_ERROR_RSRC_NFOUND;
	p = nil;
	for(tab = optabs; *tab != nil; tab++)
		if((*tab)->intf == ptype && (*tab)->open != nil){
			rc = (*tab)->open(drm, f, nf, mode, timeout, &p);
			if(rc >= 0)
				break;
		}
	if(rc >= 0){
		assert(p != nil);
		p->tab = *tab;
		newattrs(p, VI_ATTR_RSRC_CLASS, 0, class);
		newattrs(p, VI_ATTR_RSRC_NAME, 0, (char*)name);
		newattri(p, VI_ATTR_INTF_TYPE, ATINT16|ATRO, ptype);
		newattri(p, VI_ATTR_INTF_NUM, ATINT16|ATRO, bnum);
		newattri(p, VI_ATTR_TMO_VALUE, ATINT32, 2000);
		if(vi != nil) *vi = p->id;
	}
	free(buf);
	return viend(drm, rc);
}

static void
attrfree(Session *p)
{
	Attr *a, *b;
	
	for(a = p->attr; a != nil; a = b){
		b = a->next;
		free(a->str);
		free(a);
	}
}

ViStatus _VI_FUNC EXPORT
viClose(ViSession sesn)
{
	Session *p;

	if(sesn == 0)
		return VI_WARN_NULL_OBJECT;
	wlock(&sesslock);
	p = sesstab[sesn];
	sesstab[sesn] = nil;
	wunlock(&sesslock);
	if(p == nil)
		return VI_ERROR_INV_SESSION;
	lock(&p->l);
	bufwrflush(p, 1);
	if(p->tab->close != nil)
		p->tab->close(p);
	free(p->rdbuf);
	free(p->wrbuf);
	attrfree(p);
	p->drmnext->drmprev = p->drmprev;
	p->drmprev->drmnext = p->drmnext;
	unlock(&p->l);
	putlock(&p->l);
	return VI_SUCCESS;
}

static void
drmclose(Session *p)
{
	Session *q, *r;

	for(q = p->drmnext; q != p; q = r){
		r = q->drmnext;
		viClose(q->id);
	}
}

ViStatus _VI_FUNC EXPORT
viGetAttribute(ViSession sesn, ViAttr attr, void *out)
{
	Session *p;
	Attr *a;
	
	p = vibegin(sesn, -1);
	if(p == nil)
		return VI_ERROR_INV_SESSION;
	a = getattr(p, attr, 0);
	if(a == nil)
		return viend(p, VI_ERROR_NSUP_ATTR);
	if(a->get != nil)
		return viend(p, a->get(p, a, out));
	switch(a->flags & ATTYPE){
	case ATINT8: *(ViUInt8*)out = a->val; break;
	case ATINT16: *(ViUInt16*)out = a->val; break;
	case ATINT32: *(ViUInt32*)out = a->val; break;
	case ATINT64: *(ViUInt64*)out = a->val; break;
	case ATPTR: *(uintptr_t*)out = a->val; break;
	case ATSTRING: snprintf(out, 256, "%s", a->str); break;
	default: return viend(p, VI_ERROR_SYSTEM_ERROR);
	}
	return viend(p, VI_SUCCESS);
}

ViStatus _VI_FUNC EXPORT
viSetAttribute(ViSession sesn, ViAttr attr, ViAttrState in)
{
	Session *p;
	Attr *a;
	
	p = vibegin(sesn, -1);
	if(p == nil)
		return VI_ERROR_INV_SESSION;
	a = getattr(p, attr, 0);
	if(a == nil)
		return viend(p, VI_ERROR_NSUP_ATTR);
	if((a->flags & ATRO) != 0)
		return viend(p, VI_ERROR_ATTR_READONLY);
	if(a->set != nil)
		return viend(p, a->set(p, a, in));
	switch(a->flags & ATTYPE){
	case ATINT8: a->val = (ViUInt8) in; break;
	case ATINT16: a->val = (ViUInt16) in; break;
	case ATINT32: a->val = (ViUInt32) in; break;
	case ATINT64: a->val = (ViUInt64) in; break;
	case ATPTR: a->val = (uintptr_t) in; break;
	default: assert(0); return viend(p, VI_ERROR_SYSTEM_ERROR);
	}
	return viend(p, VI_SUCCESS);
}

const OpTab drmtab = {
	.intf = -1,
	.close = drmclose,
};

ViStatus _VI_FUNC EXPORT
viOpenDefaultRM(ViPSession vi)
{
	int rc;
	Session *p;
	
	if(rc = newsession(vi, &p, nil), rc < 0)
		return rc;
	p->type = SESSDRM;
	p->tab = &drmtab;
	return VI_SUCCESS;
}

#undef viGetDefaultRM
ViStatus _VI_FUNC EXPORT
viGetDefaultRM(ViPSession vi)
{
	return viOpenDefaultRM(vi);
}

ViStatus EXPORT _VI_FUNC
viLock(ViSession vi, ViAccessMode lockType, ViUInt32 timeout, ViConstKeyId requestedKey, ViChar _VI_FAR accessKey[])
{
	return VI_ERROR_NIMPL_OPER;
}

ViStatus EXPORT _VI_FUNC
viUnlock(ViSession vi)
{
	return VI_ERROR_NIMPL_OPER;
}
