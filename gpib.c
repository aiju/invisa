#include "dat.h"
#include "fns.h"

#define TALK 0x20
#define UNT 0x3f
#define LISTEN 0x40
#define UNL 0x5f
#define SAD(x) (LISTEN|(x)&0x1ff)
#define LLO 0x11
#define GET 0x08
#define TCT 0x09

GpibIntfc gpibifs;
Lock ifslock;

extern const GpibTab hjgpibtab;

const GpibTab *gpibtabs[] = {
	&hjgpibtab,
	nil,
};

void
gpibinit(void)
{
	gpibifs.next = gpibifs.prev = &gpibifs;
	newlock(&ifslock);
}

GpibIntfc *
newgpibintfc(const GpibTab *tab, size_t sz)
{
	GpibIntfc *p;
	
	p = calloc(sizeof(GpibIntfc), 1);
	if(p == nil) return nil;
	if(sz != 0){
		p->aux = calloc(sz, 1);
		if(p->aux == nil){
			free(p);
			return nil;
		}
	}
	newlock(&p->l);
	p->next = p->prev = p;
	p->tab = tab;
	p->state = GPIBIFSC;
	p->talkpad = UNKADDR;
	p->listenpad = UNKADDR;
	if(p->tab->upstate != nil)
		p->tab->upstate(p, -1);
	return p;
}

static GpibIntfc *
getintfc(int bnum, int dolock)
{
	GpibIntfc *p;

	if(dolock) lock(&ifslock);
	for(p = gpibifs.next; p != &gpibifs; p = p->next)
		if(p->bnum == bnum)
			break;
	if(p == &gpibifs)
		p = nil;
	else{
		lock(&p->l);
		p->ref++;
		unlock(&p->l);
	}
	if(dolock) unlock(&ifslock);
	return p;
}

static void
putintfc(GpibIntfc *p, int unchain, int dolock)
{
	if(dolock) lock(&ifslock);
	lock(&p->l);
	if(unchain){
		p->prev->next = p->next;
		p->next->prev = p->prev;
		p->next = p->prev = p;
	}
	if(--p->ref == 0){
		if(p->tab->destroy != nil)
			p->tab->destroy(p);
		unlock(&p->l);
		putlock(&p->l);
		free(p);
	}else
		unlock(&p->l);
	if(dolock) unlock(&ifslock);
}

static void
listinsert(GpibIntfc *p)
{
	GpibIntfc *q;
	
	for(q = gpibifs.next; q->next != &gpibifs; q = q->next)
		if(q->next->bnum > q->bnum + 1)
			break;
	if(q == &gpibifs)
		p->bnum = 0;
	else
		p->bnum = q->bnum + 1;
	p->next = q->next;
	p->prev = q;
	p->next->prev = p;
	p->prev->next = p;
}

static void
checklist(void)
{
	GpibIntfc *p, *q;
	const GpibTab **tab;

	lock(&ifslock);
	for(p = gpibifs.next; p != &gpibifs; p = q){
		q = p->next;
		if(p->tab->stillalive != nil && !p->tab->stillalive(p))
			putintfc(p, 1, 0);
	}
	for(tab = gpibtabs; *tab != nil; tab++)
		if((*tab)->enumerate(&p) >= 0)
			listinsert(p);
	unlock(&ifslock);
}

ViStatus
getlinestate(Session *p, Attr *a, void *out)
{
	ViUInt8 state, cap;
	int rc;
	GpibIntfc *ip;
	
	ip = p->aux;
	lock(&ip->l);
	if(ip->tab->lines == nil){
		state = 0;
		cap = 0;
	}else{
		rc = ip->tab->lines(ip, &state, &cap);
		if(rc < 0)
			return rc;
	}
	switch(a->attr){
	case VI_ATTR_GPIB_REN_STATE: *(short*)out = (cap & LINEREN) == 0 ? VI_STATE_UNKNOWN : (state & LINEREN) != 0 ? VI_STATE_ASSERTED : VI_STATE_UNASSERTED; break;
	case VI_ATTR_GPIB_SRQ_STATE: *(short*)out = (cap & LINESRQ) == 0 ? VI_STATE_UNKNOWN : (state & LINESRQ) != 0 ? VI_STATE_ASSERTED : VI_STATE_UNASSERTED; break;
	case VI_ATTR_GPIB_NDAC_STATE: *(short*)out = (cap & LINENDAC) == 0 ? VI_STATE_UNKNOWN : (state & LINENDAC) != 0 ? VI_STATE_ASSERTED : VI_STATE_UNASSERTED; break;
	case VI_ATTR_GPIB_ATN_STATE: *(short*)out = (cap & LINEATN) == 0 ? VI_STATE_UNKNOWN : (state & LINEATN) != 0 ? VI_STATE_ASSERTED : VI_STATE_UNASSERTED; break;
	}
	unlock(&ip->l);
	return VI_SUCCESS;
}

ViStatus
getstateflag(Session *p, Attr *a, void *out)
{
	GpibIntfc *ip;
	int rc, f;
	
	ip = p->aux;
	lock(&ip->l);
	if(ip->tab->upstate != nil){
		switch(a->attr){
		case VI_ATTR_GPIB_CIC_STATE: f = GPIBIFCIC; break;
		case VI_ATTR_GPIB_SYS_CNTRL_STATE: f = GPIBIFSC; break;
		case VI_ATTR_GPIB_ADDR_STATE: f = GPIBIFTALK | GPIBIFLISTEN; break;
		default: f = 0;
		}
		rc = ip->tab->upstate(ip, f);
		if(rc < 0)
			return rc;
	}
	switch(a->attr){
	case VI_ATTR_GPIB_CIC_STATE: *(ViBoolean*)out = (ip->state & GPIBIFCIC) != 0; break;
	case VI_ATTR_GPIB_SYS_CNTRL_STATE: *(ViBoolean*)out = (ip->state & GPIBIFSC) != 0; break;
	case VI_ATTR_GPIB_ADDR_STATE: *(ViUInt16*)out = (ip->state & GPIBIFTALK) != 0 ? VI_GPIB_TALKER : (ip->state & GPIBIFLISTEN) != 0 ? VI_GPIB_LISTENER : VI_GPIB_UNADDRESSED; break;
	}
	unlock(&ip->l);
	return VI_SUCCESS;
}

ViStatus
gpibintfcopen(Session *drm, RsrcId *id, ViAccessMode mode, ViUInt32 timeout, Session **rp)
{
	GpibIntfc *ip;
	Session *p;
	int rc;

	if(id->nf != 2)
		return VI_ERROR_INV_RSRC_NAME;
	checklist();
	ip = getintfc(id->bnum, 1);
	if(ip == nil)
		return VI_ERROR_RSRC_NFOUND;
	rc = newsession(nil, &p, drm);
	if(rc < 0){
		putintfc(ip, 0, 1);
		return rc;
	}
	p->aux = ip;
	genericattr(p, "INTFC");
	newattri(p, VI_ATTR_GPIB_PRIMARY_ADDR, ATINT16, 0);
	newattri(p, VI_ATTR_GPIB_SECONDARY_ADDR, ATINT16, VI_NO_SEC_ADDR);
	newattrx(p, VI_ATTR_GPIB_REN_STATE, ATINT16|ATRO, 0, getlinestate, nil);
	newattrx(p, VI_ATTR_GPIB_ATN_STATE, ATINT16|ATRO, 0, getlinestate, nil);
	newattrx(p, VI_ATTR_GPIB_NDAC_STATE, ATINT16|ATRO, 0, getlinestate, nil);
	newattrx(p, VI_ATTR_GPIB_SRQ_STATE, ATINT16|ATRO, 0, getlinestate, nil);
	newattrx(p, VI_ATTR_GPIB_CIC_STATE, ATBOOL|ATRO, 0, getstateflag, nil);
	newattrx(p, VI_ATTR_GPIB_SYS_CNTRL_STATE, ATBOOL|ATRO, 0, getstateflag, nil);
	newattri(p, VI_ATTR_GPIB_HS488_CBL_LEN, ATINT16|ATRO, VI_GPIB_HS488_NIMPL);
	newattrx(p, VI_ATTR_GPIB_ADDR_STATE, ATINT16|ATRO, 0, getstateflag, nil);
	return VI_SUCCESS;
}

void
gpibintfcclose(Session *p)
{
	putintfc(p->aux, 0, 1);
}

ViStatus
gpibintfcread(Session *sp, void *buf, ViUInt32 n, ViUInt32 *ret)
{
	GpibIntfc *p;
	int rc;
	
	p = sp->aux;
	lock(&p->l);
	if(p->tab->read == nil){
		rc = VI_ERROR_NSUP_OPER;
		goto out;
	}
	if(p->tab->upstate != nil && (rc = p->tab->upstate(p, GPIBIFLISTEN|GPIBIFATN|GPIBIFCIC), rc < 0))
		goto out;
	if((p->state & GPIBIFLISTEN) == 0){
		rc = VI_ERROR_INV_SETUP;
		goto out;
	}
	if((p->state & GPIBIFATN) != 0){
		if((p->state & GPIBIFCIC) != 0 && p->tab->setatn != nil){
			rc = p->tab->setatn(p, VI_GPIB_ATN_DEASSERT, attrval(sp, VI_ATTR_TMO_VALUE, 2000));
			if(rc >= 0) p->lastcmd = 0;
		}else
			rc = VI_ERROR_INV_SETUP;
		if(rc < 0)
			goto out;
	}
	rc = p->tab->read(p, buf, n, ret, attrval(sp, VI_ATTR_SUPPRESS_END_EN, 0), attrval(sp, VI_ATTR_TERMCHAR_EN, 1) ? attrval(sp, VI_ATTR_TERMCHAR, '\n') : -1, attrval(sp, VI_ATTR_TMO_VALUE, 2000));
out:
	unlock(&p->l);
	return rc;
}

ViStatus
gpibintfcwrite(Session *sp, void *buf, ViUInt32 n, ViUInt32 *ret, int end)
{
	GpibIntfc *p;
	int rc;
	
	p = sp->aux;
	lock(&p->l);
	if(p->tab->read == nil){
		rc = VI_ERROR_NSUP_OPER;
		goto out;
	}
	if(p->tab->upstate != nil && (rc = p->tab->upstate(p, GPIBIFTALK|GPIBIFATN|GPIBIFCIC), rc < 0))
		goto out;
	if((p->state & GPIBIFTALK) == 0){
		rc = VI_ERROR_INV_SETUP;
		goto out;
	}
	if((p->state & GPIBIFATN) != 0){
		if((p->state & GPIBIFCIC) != 0 && p->tab->setatn != nil){
			rc = p->tab->setatn(p, VI_GPIB_ATN_DEASSERT, attrval(sp, VI_ATTR_TMO_VALUE, 2000));
			if(rc >= 0) p->lastcmd = 0;
		}else
			rc = VI_ERROR_INV_SETUP;
		if(rc < 0)
			goto out;
	}
	rc = p->tab->write(p, buf, n, ret, end, attrval(sp, VI_ATTR_TMO_VALUE, 2000));
out:
	unlock(&p->l);
	return rc;
}

static ViStatus
gpibcmd(GpibIntfc *p, char *buf, ViUInt32 n, ViUInt32 *ret, ViUInt32 tmo)
{
	int rc, i;

	if(p->tab->write == nil || p->tab->setatn == nil)
		return VI_ERROR_NSUP_OPER;
	if(p->tab->upstate != nil && (rc = p->tab->upstate(p, GPIBIFCIC), rc < 0))
		return rc;
	if((p->state & GPIBIFCIC) == 0)
		return VI_ERROR_NCIC;
	if(rc = p->tab->setatn(p, VI_GPIB_ATN_ASSERT, tmo), rc < 0)
		return rc;
	rc = p->tab->write(p, buf, n, ret, 0, tmo);
	if(rc >= 0)
		for(i = 0; i < n; i++){
			switch((ViByte)buf[i] >> 6){
			case 1:
				p->talksad = NOADDR;
				p->talkpad = buf[i] == UNT ? NOADDR : buf[i] & 31;
				break;
			case 2:
				p->listensad = NOADDR;
				if(buf[i] == UNL)
					p->listenpad = NOADDR;
				else if(p->listenpad != NOADDR && (p->listenpad != (buf[i] & 31) || p->listensad != NOADDR))
					p->listenpad = MULTIADDR;
				else
					p->listenpad = buf[i] & 31;
				break;
			case 3:
				switch(p->lastcmd >> 6){
				case 1:
					if(p->lastcmd != UNT && (ViByte)buf[i] != 0x7f)
						p->talksad = buf[i] & 31;
					break;
				case 2:
					if(p->lastcmd != UNL && (ViByte)buf[i] != 0x7f && p->listenpad != MULTIADDR)
						p->listensad = buf[i] & 31;
					break;
				}
			}
			p->lastcmd = buf[i];
		}
	return rc;
}

static ViStatus
gpibcmdv(GpibIntfc *p, ViUInt32 tmo, ...)
{
	va_list va;
	char *buf, *nb;
	int nbuf;
	int c, rc;
	
	buf = nil;
	nbuf = 0;
	va_start(va, tmo);
	while(c = va_arg(va, int), c >= 0){
		if((c & 0x100) != 0) continue;
		if((nbuf & 63) == 0){
			nb = realloc(buf, nbuf + 64);
			if(nb == nil){
				free(nb);
				free(buf);
				va_end(va);
				return VI_ERROR_ALLOC;
			}
			buf = nb;
		}
		buf[nbuf++] = c;
	}
	va_end(va);
	rc = gpibcmd(p, buf, nbuf, nil, tmo);
	free(buf);
	return rc;
}

ViStatus
gpibintfccommand(Session *sp, void *buf, ViUInt32 n, ViUInt32 *ret)
{
	GpibIntfc *p;
	int rc;
	
	p = sp->aux;
	lock(&p->l);
	rc = gpibcmd(p, buf, n, ret, attrval(sp, VI_ATTR_TMO_VALUE, 2000));
	unlock(&p->l);
	return rc;
}

ViStatus
gpibintfcren(Session *sp, ViUInt16 mode)
{
	GpibIntfc *p;
	int rc;
	
	p = sp->aux;
	if(p->tab->setren == nil) return VI_ERROR_NSUP_OPER;
	lock(&p->l);
	if(p->tab->upstate != nil && (rc = p->tab->upstate(p, GPIBIFCIC), rc < 0))
		goto out;
	if((p->state & GPIBIFCIC) == 0){
		rc = VI_ERROR_NCIC;
		goto out;
	}
	switch(mode){
	case VI_GPIB_REN_DEASSERT: rc = p->tab->setren(p, 0, attrval(sp, VI_ATTR_TMO_VALUE, 2000)); break;
	case VI_GPIB_REN_ASSERT: rc = p->tab->setren(p, 1, attrval(sp, VI_ATTR_TMO_VALUE, 2000)); break;
	case VI_GPIB_REN_ASSERT_LLO: rc = gpibcmdv(p, attrval(sp, VI_ATTR_TMO_VALUE, 2000), LLO, -1); break;
	default: rc = VI_ERROR_INV_MODE;
	}
out:
	unlock(&p->l);
	return rc;
}

ViStatus
gpibintfcatn(Session *sp, ViUInt16 mode)
{
	GpibIntfc *p;
	int rc;
	
	p = sp->aux;
	if(p->tab->setatn == nil) return VI_ERROR_NSUP_OPER;
	lock(&p->l);
	if(p->tab->upstate != nil && (rc = p->tab->upstate(p, GPIBIFCIC), rc < 0))
		goto out;
	if((p->state & GPIBIFCIC) == 0){
		rc = VI_ERROR_NCIC;
		goto out;
	}
	rc = p->tab->setatn(p, mode, attrval(sp, VI_ATTR_TMO_VALUE, 2000));
	if(rc >= 0)
		p->lastcmd = 0;
out:
	unlock(&p->l);
	return rc;
}

ViStatus
gpibintfcifc(Session *sp)
{
	GpibIntfc *p;
	int rc;
	
	p = sp->aux;
	if(p->tab->sendifc == nil) return VI_ERROR_NSUP_OPER;
	lock(&p->l);
	if(p->tab->upstate != nil && (rc = p->tab->upstate(p, GPIBIFSC), rc < 0))
		goto out;
	if((p->state & GPIBIFSC) == 0){
		rc = VI_ERROR_NSYS_CNTLR;
		goto out;
	}
	rc = p->tab->sendifc(p, attrval(sp, VI_ATTR_TMO_VALUE, 2000));
	if(rc >= 0)
		p->state |= GPIBIFCIC;
out:
	unlock(&p->l);
	return rc;
}

ViStatus
gpibintfcpasscontrol(Session *sp, ViUInt16 pad, ViUInt16 sad)
{
	GpibIntfc *p;
	int rc;
	
	p = sp->aux;
	lock(&p->l);
	rc = gpibcmdv(p, attrval(sp, VI_ATTR_TMO_VALUE, 2000), UNT, UNL, LISTEN|pad, SAD(sad), TCT, -1);
	if(rc >= 0)
		p->state &= ~GPIBIFCIC;
	unlock(&p->l);
	return rc;
}

ViStatus
gpibintfctrigger(Session *sp, ViUInt16 protocol)
{
	GpibIntfc *p;
	int rc;
	
	if(protocol != VI_TRIG_PROT_DEFAULT)
		return VI_ERROR_INV_PROT;
	p = sp->aux;
	lock(&p->l);
	rc = gpibcmdv(p, attrval(sp, VI_ATTR_TMO_VALUE, 2000), GET, -1);
	unlock(&p->l);
	return rc;
}

const OpTab gpibintfctab = {
	.intf = VI_INTF_GPIB,
	.class = CLASS_INTFC,
	.open = gpibintfcopen,
	.close = gpibintfcclose,
	.read = gpibintfcread,
	.write = gpibintfcwrite,
	.gpibControlREN = gpibintfcren,
	.gpibControlATN = gpibintfcatn,
	.gpibSendIFC = gpibintfcifc,
	.gpibCommand = gpibintfccommand,
	.gpibPassControl = gpibintfcpasscontrol,
	.assertTrigger = gpibintfctrigger,
};

ViStatus EXPORT _VI_FUNC
viGpibCommand (ViSession vi, ViConstBuf buf, ViUInt32 count, ViPUInt32 retc)
{
	Session *p;
	
	if(retc != nil) *retc = 0;
	p = vibegin(vi, -1);
	if(p == nil) return VI_ERROR_INV_SESSION;
	if(p->tab->gpibCommand == nil) return viend(p, VI_ERROR_NSUP_OPER);
	return viend(p, p->tab->gpibCommand(p, (void *) buf, count, retc));
}

ViStatus EXPORT _VI_FUNC
viGpibControlREN(ViSession vi, ViUInt16 mode)
{
	Session *p;

	p = vibegin(vi, -1);
	if(p == nil) return VI_ERROR_INV_SESSION;
	if(p->tab->gpibControlREN == nil) return viend(p, VI_ERROR_NSUP_OPER);
	return viend(p, p->tab->gpibControlREN(p, mode));
}

ViStatus EXPORT _VI_FUNC
viGpibControlATN(ViSession vi, ViUInt16 mode)
{
	Session *p;

	p = vibegin(vi, -1);
	if(p == nil) return VI_ERROR_INV_SESSION;
	if(p->tab->gpibControlATN == nil) return viend(p, VI_ERROR_NSUP_OPER);
	return viend(p, p->tab->gpibControlATN(p, mode));
}

ViStatus EXPORT _VI_FUNC
viGpibSendIFC (ViSession vi)
{
	Session *p;

	p = vibegin(vi, -1);
	if(p == nil) return VI_ERROR_INV_SESSION;
	if(p->tab->gpibSendIFC == nil) return viend(p, VI_ERROR_NSUP_OPER);
	return viend(p, p->tab->gpibSendIFC(p));
}

ViStatus EXPORT _VI_FUNC
viGpibPassControl(ViSession vi, ViUInt16 primAddr, ViUInt16 secAddr)
{
	Session *p;

	p = vibegin(vi, -1);
	if(p == nil) return VI_ERROR_INV_SESSION;
	if(p->tab->gpibPassControl == nil) return viend(p, VI_ERROR_NSUP_OPER);
	return viend(p, p->tab->gpibPassControl(p, primAddr, secAddr));
}
