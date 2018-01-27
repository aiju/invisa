#include "dat.h"
#include "fns.h"

ViStatus
statusor(ViStatus a, ViStatus b)
{
	return a < 0 ? a : b;
}

ViStatus EXPORT _VI_FUNC
viTerminate(ViSession vi, ViUInt16 degree, ViJobId job)
{
	return VI_ERROR_NIMPL_OPER;
}

ViStatus EXPORT _VI_FUNC
viRead(ViSession vi, ViByte *buf, ViUInt32 count, ViUInt32 *retc)
{
	Session *p;
	
	if(retc != nil) *retc = 0;
	p = vibegin(vi, -1);
	if(p == nil) return VI_ERROR_INV_SESSION;
	if(p->tab->read == nil) return viend(p, VI_ERROR_NSUP_OPER);
	return viend(p, p->tab->read(p, buf, count, retc));
}

ViStatus EXPORT _VI_FUNC
viReadToFile(ViSession vi, ViConstString name, ViUInt32 count, ViUInt32 *retc)
{
	Session *p;
	FILE *f;
	char buf[16384];
	ViUInt32 n, m, t, s;
	int rc;
	
	if(retc != nil) *retc = 0;
	p = vibegin(vi, -1);
	if(p == nil)
		return VI_ERROR_INV_SESSION;
	if(p->tab->read == nil)
		return viend(p, VI_ERROR_NSUP_OPER);
	f = fopen(name, attrval(p, VI_ATTR_FILE_APPEND_EN, 0) ? "ab" : "wb");
	if(f == nil)
		return viend(p, VI_ERROR_FILE_ACCESS);
	rc = VI_SUCCESS_MAX_CNT;
	for(n = 0; n < count && rc == VI_SUCCESS_MAX_CNT; n += s){
		m = sizeof(buf);
		if(m > count - n) m = count - n;
		t = 0;
		rc = p->tab->read(p, buf, m, &t);
		if(t == 0) break;
		s = fwrite(buf, 1, t, f);
		if(s < t && rc >= 0)
			rc = VI_ERROR_FILE_IO;
	}
	if(retc != nil) *retc = n;
	fclose(f);
	return viend(p, rc);
}

ViStatus EXPORT _VI_FUNC
viReadAsync(ViSession vi, ViByte *buf, ViUInt32 count, ViJobId *job)
{
	return VI_ERROR_NIMPL_OPER;
}

ViStatus EXPORT _VI_FUNC
viWrite(ViSession vi, const ViByte *buf, ViUInt32 count, ViUInt32 *retc)
{
	Session *p;
	
	if(retc != nil) *retc = 0;
	p = vibegin(vi, -1);
	if(p == nil) return VI_ERROR_INV_SESSION;
	if(p->tab->write == nil) return viend(p, VI_ERROR_NSUP_OPER);
	return viend(p, p->tab->write(p, (void *) buf, count, retc, attrval(p, VI_ATTR_SEND_END_EN, 1)));
}

/* BUG: doesn't send END indicator */
ViStatus EXPORT _VI_FUNC
viWriteFromFile(ViSession vi, ViConstString name, ViUInt32 count, ViUInt32 *retc)
{
	Session *p;
	FILE *f;
	char buf[16384];
	ViUInt32 n, m, t, s;
	int rc;
	
	if(retc != nil) *retc = 0;
	p = vibegin(vi, -1);
	if(p == nil)
		return VI_ERROR_INV_SESSION;
	if(p->tab->write == nil)
		return viend(p, VI_ERROR_NSUP_OPER);
	f = fopen(name, "rb");
	if(f == nil)
		return viend(p, VI_ERROR_FILE_ACCESS);
	rc = VI_SUCCESS;
	for(n = 0; n < count && !feof(f) && rc == VI_SUCCESS; n += t){
		m = sizeof(buf);
		if(m > count - n) m = count - n;
		t = fread(buf, 1, m, f);
		if(ferror(f))
			rc = VI_ERROR_FILE_IO;
		if(t == 0)
			break;
		s = 0;
		rc = statusor(rc, p->tab->write(p, buf, t, &s, 0));
		if(rc >= 0 && s < t)
			rc = VI_ERROR_IO;
	}
	if(retc != nil) *retc = n;
	fclose(f);
	return viend(p, rc);
}

ViStatus EXPORT _VI_FUNC
viWriteAsync(ViSession vi, const ViByte *buf, ViUInt32 count, ViJobId *job)
{
	return VI_ERROR_NIMPL_OPER;
}

ViStatus EXPORT _VI_FUNC
viAssertTrigger(ViSession vi, ViUInt16 prot)
{
	Session *p;

	p = vibegin(vi, -1);
	if(p == nil) return VI_ERROR_INV_SESSION;
	if(p->tab->assertTrigger == nil) return viend(p, VI_ERROR_NSUP_OPER);
	return viend(p, p->tab->assertTrigger(p, prot));
}

ViStatus EXPORT _VI_FUNC
viReadSTB(ViSession vi, ViUInt16 *status)
{
	Session *p;

	p = vibegin(vi, -1);
	if(p == nil) return VI_ERROR_INV_SESSION;
	if(p->tab->readSTB == nil) return viend(p, VI_ERROR_NSUP_OPER);
	return viend(p, p->tab->readSTB(p, status));
}

static ViStatus
mkbuf(Session *p, Buf **bp, int size)
{
	Buf *b;
	
	if(size < 1) return VI_ERROR_INV_SETUP;
	b = calloc(sizeof(Buf) - 1 + size, 1);
	if(b == nil) return VI_ERROR_ALLOC;
	b->n = size;
	b->unget = -1;
	b->end = VI_SUCCESS_MAX_CNT;
	free(*bp);
	*bp = b;
	return VI_SUCCESS;
}

ViStatus
bufread(Session *p, void *v, ViUInt32 n, ViUInt32 *retc)
{
	int rc;
	char *vv;
	size_t r, m;
	Buf *b;

	if(p->rdbuf == nil && (rc = mkbuf(p, &p->rdbuf, attrval(p, VI_ATTR_RD_BUF_SIZE, DEFBUF)), rc < 0)){
		if(retc != nil) *retc = 0;
		return rc;
	}
	b = p->rdbuf;
	vv = v;
	rc = VI_SUCCESS_MAX_CNT;
	r = 0;
	if(n > 0 && b->unget >= 0){
		vv[r++] = b->unget;
		b->unget = -1;
		if(b->end != VI_SUCCESS_MAX_CNT){
			rc = b->end;
			b->end = 0;
		}
	}
	for(; r < n && rc == VI_SUCCESS_MAX_CNT; ){
		if(b->rd >= b->wr){
			b->rd = b->wr = 0;
			if(b->end != VI_SUCCESS_MAX_CNT){
				rc = b->end;
				b->end = 0;
			}else
				rc = p->tab->read(p, b->b, b->n, &b->wr);
		}
		m = b->wr - b->rd;
		if(m > n - r) m = n - r;
		memcpy(vv + r, b->b + b->rd, m);
		r += m;
		b->rd += m;
	}
	if(rc != VI_SUCCESS_MAX_CNT && b->wr > b->rd){
		b->end = rc;
		rc = VI_SUCCESS_MAX_CNT;
	}
	if(retc != nil) *retc = r;
	return rc;
}

ViStatus
bufungetc(Session *p, ViByte c, int end)
{
	int rc;

	if(p->rdbuf == nil && (rc = mkbuf(p, &p->rdbuf, attrval(p, VI_ATTR_RD_BUF_SIZE, DEFBUF)), rc < 0))
		return rc;
	assert(p->rdbuf->unget < 0);
	p->rdbuf->unget = c;
	p->rdbuf->end = end;
	return VI_SUCCESS;
}

void
bufrdflush(Session *p)
{
	if(p->rdbuf != nil){
		p->rdbuf->rd = p->rdbuf->wr = 0;
		p->rdbuf->unget = -1;
		p->rdbuf->end = VI_SUCCESS_MAX_CNT;
	}
}

ViStatus
bufwrflush(Session *p, int end)
{
	int rc;
	int nwr;
	ViUInt32 t;

	if(p->wrbuf == nil || p->wrbuf->wr == 0)
		return VI_SUCCESS;
	nwr = p->wrbuf->wr;
	if(!end) nwr--;
	rc = p->tab->write(p, p->wrbuf->b, nwr, &t, end && attrval(p, VI_ATTR_SEND_END_EN, 1));
	if(rc >= 0 && t < nwr)
		rc = VI_ERROR_IO;
	if(rc < 0 && !end){
		p->wrbuf->b[0] = p->wrbuf->b[p->wrbuf->wr - 1];
		p->wrbuf->wr = 1;
	}else
		p->wrbuf->wr = 0;
	return rc;
}

ViStatus
bufwrite(Session *p, void *v, ViUInt32 n, ViUInt32 *retc)
{
	int rc;
	char *vv;
	size_t r, m;
	Buf *b;

	if(p->wrbuf == nil && (rc = mkbuf(p, &p->wrbuf, attrval(p, VI_ATTR_WR_BUF_SIZE, DEFBUF)), rc < 0)){
		if(retc != nil) *retc = 0;
		return rc;
	}
	b = p->wrbuf;
	vv = v;
	rc = VI_SUCCESS;
	for(r = 0; r < n && rc >= 0; ){
		m = b->n - b->wr;
		if(m > n - r) m = n - r;
		memcpy(b->b + b->wr, vv + r, m);
		r += m;
		b->wr += m;
		if(b->wr == b->n)
			rc = bufwrflush(p, 0);
	}
	return rc;
}

ViStatus EXPORT _VI_FUNC
viSetBuf(ViSession vi, ViUInt16 mask, ViUInt32 size)
{
	Session *p;
	int rc, rc2;
	
	if((mask & ~(VI_READ_BUF|VI_WRITE_BUF|VI_IO_IN_BUF|VI_IO_OUT_BUF)) != 0) return VI_ERROR_INV_MASK;
	p = vibegin(vi, -1);
	if(p == nil) return VI_ERROR_INV_SESSION;
	rc = VI_SUCCESS;
	if((mask & VI_READ_BUF) != 0){
		rc = mkbuf(p, &p->rdbuf, size);
		if(rc >= 0)
			newattri(p, VI_ATTR_RD_BUF_SIZE, ATINT32|ATRO, size);
	}
	if((mask & VI_WRITE_BUF) != 0){
		bufwrflush(p, 1);
		rc2 = mkbuf(p, &p->wrbuf, size);
		if(rc2 >= 0)
			newattri(p, VI_ATTR_WR_BUF_SIZE, ATINT32|ATRO, size);
		rc = statusor(rc, rc2);
	}
	if((mask & (VI_IO_IN_BUF|VI_IO_OUT_BUF)) != 0)
		rc = statusor(rc, VI_WARN_NSUP_BUF);
	return viend(p, rc);
}

ViStatus EXPORT _VI_FUNC
viFlush(ViSession vi, ViUInt16 mask)
{
	Session *p;
	int rc;
	
	if((mask & ~(VI_READ_BUF|VI_READ_BUF_DISCARD|VI_WRITE_BUF|VI_WRITE_BUF_DISCARD|VI_IO_IN_BUF|VI_IO_IN_BUF_DISCARD|VI_IO_OUT_BUF|VI_IO_OUT_BUF_DISCARD)) != 0) return VI_ERROR_INV_MASK;
	if((mask & (VI_READ_BUF|VI_READ_BUF_DISCARD)) == (VI_READ_BUF|VI_READ_BUF_DISCARD)) return VI_ERROR_INV_MASK;
	if((mask & (VI_WRITE_BUF|VI_WRITE_BUF_DISCARD)) == (VI_WRITE_BUF|VI_WRITE_BUF_DISCARD)) return VI_ERROR_INV_MASK;
	if((mask & (VI_IO_IN_BUF|VI_IO_IN_BUF_DISCARD)) == (VI_IO_IN_BUF|VI_IO_IN_BUF_DISCARD)) return VI_ERROR_INV_MASK;
	if((mask & (VI_IO_OUT_BUF|VI_IO_OUT_BUF_DISCARD)) == (VI_IO_OUT_BUF|VI_IO_OUT_BUF_DISCARD)) return VI_ERROR_INV_MASK;
	p = vibegin(vi, -1);
	if(p == nil) return VI_ERROR_INV_SESSION;
	rc = VI_SUCCESS;
	if((mask & VI_WRITE_BUF) != 0) rc = bufwrflush(p, 1);
	if((mask & VI_WRITE_BUF_DISCARD) != 0 && p->wrbuf != nil) p->wrbuf->wr = 0;
	if((mask & VI_READ_BUF_DISCARD) != 0) bufrdflush(p);
	if((mask & VI_READ_BUF) != 0 && p->rdbuf != nil && (p->rdbuf->rd < p->rdbuf->wr || p->rdbuf->unget >= 0)){
		bufrdflush(p);
		if(attrval(p, VI_ATTR_SUPPRESS_END_EN, 0) == 0 || attrval(p, VI_ATTR_TERMCHAR_EN, 0) != 0)
			while(p->tab->read(p, p->rdbuf, p->rdbuf->n, nil) == VI_SUCCESS_MAX_CNT)
				;
	}
	return viend(p, rc);
}

ViStatus EXPORT _VI_FUNC
viClear(ViSession vi)
{
	Session *p;
	int rc;

	p = vibegin(vi, -1);
	if(p == nil) return VI_ERROR_INV_SESSION;
	bufrdflush(p);
	if(p->wrbuf != nil) p->wrbuf->wr = 0;
	if(p->tab->clear != nil)
		rc = p->tab->clear(p);
	else
		rc = VI_SUCCESS;
	return viend(p, rc);
}

ViStatus EXPORT _VI_FUNC
viBufRead(ViSession vi, ViByte *buf, ViUInt32 n, ViUInt32 *retc)
{
	Session *p;

	p = vibegin(vi, -1);
	if(p == nil) return VI_ERROR_INV_SESSION;
	if(p->tab->read == nil) return VI_ERROR_NSUP_OPER;
	return viend(p, bufread(p, buf, n, retc));	
}

ViStatus EXPORT _VI_FUNC
viBufWrite(ViSession vi, const ViByte *buf, ViUInt32 n, ViUInt32 *retc)
{
	Session *p;
	int rc;

	p = vibegin(vi, -1);
	if(p == nil) return VI_ERROR_INV_SESSION;
	if(p->tab->write == nil) return viend(p, VI_ERROR_NSUP_OPER);
	rc = bufwrite(p, (void*)buf, n, retc);
	if(rc >= 0)
		rc = bufwrflush(p, 1);
	else if(rc == VI_ERROR_TMO && p->wrbuf != nil)
		p->wrbuf->wr = 0;
	return viend(p, rc);
}

ViStatus
iobufread(Session *p, void *buf, ViUInt32 n, ViUInt32 *retc)
{
	Buf *b;
	int rc, rv, r, m;
	int i;
	int term;

	if(p->tab->rawread == nil)
		return VI_ERROR_NSUP_OPER;
	if(p->iordbuf == nil && (rc = mkbuf(p, &p->iordbuf, DEFBUF)) < 0)
		return rc;
	rc = VI_SUCCESS;
	b = p->iordbuf;
	if(attrval(p, VI_ATTR_TERMCHAR_EN, 0) != 0)
		term = attrval(p, VI_ATTR_TERMCHAR, -1);
	else
		term = -1;
	rc = VI_SUCCESS_MAX_CNT;
	for(r = 0; r < n && rc == VI_SUCCESS_MAX_CNT; ){
		if(b->rd >= b->wr){
			b->rd = 0;
			b->wr = 0;
			if(b->end < 0){
				rc = b->end;
				b->end = 0;
			}else{
				rv = p->tab->rawread(p, b->b, b->n, &b->wr);
				if(rv < 0)
					rc = rv;
				else if(b->wr == 0)
					rc = VI_SUCCESS;
				else
					rc = VI_SUCCESS_MAX_CNT;
			}
		}
		m = b->wr - b->rd;
		if(m > n - r) m = n - r;
		for(i = 0; i < m; i++){
			((char*)buf)[r + i] = b->b[b->rd + i];
			if((unsigned char)b->b[b->rd + i] == term){
				if(rc != VI_SUCCESS_MAX_CNT)
					b->end = rc;
				rc = VI_SUCCESS;
				break;
			}
		}
		r += i;
		b->rd += i;
	}
	printf("returning %d bytes from bufread (term=%d)\n", (int)r, term);
	if(retc != nil)
		*retc = r;
	if(r == n && rc < 0){
		b->end = rc;
		rc = VI_SUCCESS_MAX_CNT;
	}
	return rc;
}
