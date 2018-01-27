#include "dat.h"
#include "fns.h"
#include <unistd.h>

typedef struct AuxTCP AuxTCP;
struct AuxTCP {
	int fd;
};

static int
tcpconnect(char *host, char *port)
{
	int fd;
	struct addrinfo hint, *ai, *ap;

	memset(&hint, 0, sizeof(hint));
	hint.ai_family = AF_UNSPEC;
	hint.ai_socktype = SOCK_STREAM;
	if(getaddrinfo(host, port, &hint, &ai) < 0)
		return -1;
	for(ap = ai; ap != nil; ap = ap->ai_next){
		fd = socket(ap->ai_family, ap->ai_socktype, ap->ai_protocol);
		if(fd < 0)
			continue;
		if(connect(fd, ap->ai_addr, ap->ai_addrlen) < 0){
			close(fd);
			continue;
		}
		freeaddrinfo(ai);
		return fd;
	}
	return -1;
}

static ViStatus
tcptimeout(Session *p, Attr *a, ViAttrState in)
{
	AuxTCP *q;
	DWORD time;
	unsigned long nbl;

	q = p->aux;
	assert(q != nil);
	if(q->fd < 0) return VI_ERROR_CONN_LOST;
	time = in;
	nbl = in == VI_TMO_IMMEDIATE;
	time = in == VI_TMO_INFINITE ? 0 : in;
	ioctlsocket(q->fd, FIONBIO, &nbl);
	setsockopt(q->fd, SOL_SOCKET, SO_RCVTIMEO, (char*)&time, sizeof(DWORD));
	setsockopt(q->fd, SOL_SOCKET, SO_SNDTIMEO, (char*)&time, sizeof(DWORD));
	return VI_SUCCESS;
}

static ViStatus
tcpipsocketopen(Session *drm, RsrcId *id, ViAccessMode mode, ViUInt32 timeout, Session **rp)
{
	Session *p;
	AuxTCP *q;
	int rc;
	int fd;

	if(id->nf != 4)
		return VI_ERROR_NSUP_OPER;
	fd = tcpconnect(id->f[1], id->f[2]);
	if(fd < 0)
		return VI_ERROR_RSRC_NFOUND;
	if(rc = newsession(nil, &p, drm), rc < 0){
		close(fd);
		return rc;
	}
	q = calloc(sizeof(AuxTCP), 1);
	if(q == nil){
		close(fd);
		viClose(p->id);
		return VI_ERROR_ALLOC;
	}
	q->fd = fd;
	p->aux = q;
	genericattr(p, "SOCKET");
	newattrx(p, VI_ATTR_TMO_VALUE, ATINT32|ATCALLSET, 2000, nil, tcptimeout);
	*rp = p;
	return VI_SUCCESS;
}

static ViStatus
tcpipsocketread(Session *p, void *v, ViUInt32 n, ViUInt32 *retc)
{
	AuxTCP *q;
	int rc;
	
	q = p->aux;
	assert(q != nil);
	if(q->fd < 0) return VI_ERROR_CONN_LOST;
	rc = recv(q->fd, v, n, 0);
	if(rc < 0)
		switch(WSAGetLastError()){
		case WSAECONNRESET:
eof:			close(q->fd);
			q->fd = -1;
			return VI_ERROR_CONN_LOST;
		case WSAETIMEDOUT:
			return VI_ERROR_TMO;
		default:
			return VI_ERROR_IO;
		}
	if(rc == 0)
		goto eof;
	if(retc != nil)
		*retc = rc;
	return VI_SUCCESS;
}

static ViStatus
tcpipsocketwrite(Session *p, void *v, ViUInt32 n, ViUInt32 *retc, int end)
{
	AuxTCP *q;
	int rc, i, m;
	
	q = p->aux;
	assert(q != nil);
	if(q->fd < 0) return VI_ERROR_CONN_LOST;
	for(i = 0; i < n; ){
		m = n - i;
		rc = send(q->fd, (char*)v + i, m, 0);
		if(rc < 0)
			switch(WSAGetLastError()){
			case WSAECONNRESET:
			case WSAENOTCONN:
			case WSAENOTSOCK:
			case WSAESHUTDOWN:
				close(q->fd);
				q->fd = -1;
				return VI_ERROR_CONN_LOST;
			case WSAETIMEDOUT:
				return VI_ERROR_TMO;
			default:
				return VI_ERROR_IO;
			}
		i += rc;
		if(retc != nil)
			*retc = i;
	}
	return VI_SUCCESS;
}

const OpTab tcpipsockettab = {
	.intf = VI_INTF_TCPIP,
	.class = CLASS_SOCKET,
	.open = tcpipsocketopen,
	.read = iobufread,
	.rawread = tcpipsocketread,
	.write = tcpipsocketwrite,
};
