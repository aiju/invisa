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
tcpipopen(Session *drm, char **f, int nf, ViAccessMode mode, ViUInt32 timeout, Session **rp)
{
	Session *p;
	AuxTCP *q;
	int rc;
	int fd;

	if(nf != 4 || strcmp(f[3], "SOCKET") != 0)
		return VI_ERROR_NSUP_OPER;
	fd = tcpconnect(f[1], f[2]);
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
	newattri(p, VI_ATTR_TERMCHAR, ATINT8, '\n');
	newattri(p, VI_ATTR_TERMCHAR_EN, ATINT16, 0);
	*rp = p;
	return VI_SUCCESS;
}

static ViStatus
tcpipread(Session *p, void *v, ViUInt32 n, ViUInt32 *retc)
{
	AuxTCP *q;
	int rc;
	
	q = p->aux;
	assert(q != nil);
	if(q->fd < 0) return VI_ERROR_CONN_LOST;
	rc = recv(q->fd, v, n, 0);
	if(rc < 0)
		switch(errno){
		case WSAECONNRESET:
eof:			close(q->fd);
			q->fd = -1;
			return VI_ERROR_CONN_LOST;
		default:
			return VI_ERROR_IO;
		}
	if(rc == 0)
		goto eof;
	if(retc != NULL)
		*retc = rc;
	return VI_SUCCESS;
}

static ViStatus
tcpipwrite(Session *p, void *v, ViUInt32 n, ViUInt32 *retc, int end)
{
	*retc = fwrite(v, 1, n, stdout);
	if(end) printf("<END>");
	fflush(stdout);
	return VI_SUCCESS;
}

const OpTab tcpiptab = {
	.intf = VI_INTF_TCPIP,
	.open = tcpipopen,
	.read = iobufread,
	.rawread = tcpipread,
	.write = tcpipwrite,
};
