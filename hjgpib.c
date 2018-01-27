#include "dat.h"
#include "fns.h"
#include <libusb.h>
extern libusb_context *usbctxt;

#define UNL 0x3f
#define LISTEN 0x20
#define TALK 0x40
#define SAD(x) ((((UCHAR)(x))-1 & 0xff)+1)
#define CMD(x) (0x200|(x))

#define ChESC 0xf0
#define ChEOI 0xf1

#define CmdATN0 0x0
#define CmdATN1T 0x1
#define CmdATN1L 0x2
#define CmdIFC 0x3
#define CmdStatus 0x4
#define CmdReadN 0x5
#define CmdReadEOI0 0x6
#define CmdReadEOI1 0x7
#define CmdReadEOS 0x8
#define CmdREN0 0x9
#define CmdREN1 0xa
#define CmdNOP 0xb

#define USBVID 0x6666
#define USBPID 0x691B

#define MAXPKT 64

typedef struct HjAux HjAux;

struct HjAux {
	libusb_device_handle *devh;
	ViByte rxbuf[MAXPKT];
	int rxbufp, rxbufl, rxbufrc;
};

static int
hjgpibenumerate(GpibIntfc **rp)
{
	extern const GpibTab hjgpibtab;
	GpibIntfc *p;
	HjAux *ap;
	libusb_device **dl;
	struct libusb_device_descriptor desc;
	libusb_device_handle *h;
	int cnt, i;
	
	if(usbctxt == nil) return -1;
	cnt = libusb_get_device_list(usbctxt, &dl);
	for(i = 0; i < cnt; i++){
		if(libusb_get_device_descriptor(dl[i], &desc) < 0) continue;
		if(desc.idVendor != USBVID || desc.idProduct != USBPID) continue;
		if(libusb_open(dl[i], &h) < 0) continue;
		if(libusb_claim_interface(h, 0) >= 0 && libusb_set_configuration(h, 1) >= 0) break;
		libusb_close(h);
	}
	libusb_free_device_list(dl, 1);
	if(i == cnt) return -1;
	p = newgpibintfc(&hjgpibtab, sizeof(HjAux));
	if(p == nil){
		libusb_close(h);
		return -1;
	}
	ap = p->aux;
	ap->devh = h;
	if(rp != nil) *rp = p;
	return 0;
}

static ViStatus
sendraw(GpibIntfc *p, void *data, ViUInt32 len, ViUInt32 *retc, ViUInt32 tmo)
{
	int rc, txd;
	HjAux *ap;
	
	ap = p->aux;
	switch(tmo){
	case VI_TMO_IMMEDIATE: tmo = 1; break;
	case VI_TMO_INFINITE: tmo = 0; break;
	}
	if(retc != nil) *retc = 0;
	while(len > 0){
		rc = libusb_bulk_transfer(ap->devh, 0x01, data, len, &txd, tmo);
		data = (char*)data + txd;
		len -= txd;
		if(retc != nil) *retc += txd;
		if(rc < 0)
			switch(rc){
			case LIBUSB_ERROR_TIMEOUT: return VI_ERROR_TMO;
			default: return VI_ERROR_IO;
			}
	}
	return VI_SUCCESS;
}

static void
reset(GpibIntfc *p)
{
	HjAux *ap;
	
	ap = p->aux;
	libusb_set_configuration(ap->devh, 1);
}

static ViStatus
sendesc(GpibIntfc *p, void *d, ViUInt32 len, int eot, ViUInt32 *retc, ViUInt32 tmo)
{
	ViByte buf[MAXPKT];
	ViByte *c;
	int rc;
	ViUInt32 rv;
	int bp, i;

	bp = 0;
	if(retc != nil) *retc = 0;
	rc = VI_SUCCESS;
	for(c = d; ; c++){
		if(c == (UCHAR*)d + len || bp + (*c == ChESC || *c == ChEOI || eot && c+1 == (UCHAR*)d+len) == MAXPKT){
			rc = sendraw(p, buf, bp, &rv, tmo);
			bp = 0;
			if(retc != nil)
				for(i = 0; i < rv; i++)
					if(buf[i] != ChESC && buf[i] != ChEOI)
						(*retc)++;
			if(c == (UCHAR*)d + len || rc < 0)
				break;
		}
		if(eot && c+1 == (UCHAR*)d+len)
			buf[bp++] = ChEOI;
		else if(*c == ChESC || *c == ChEOI)
			buf[bp++] = ChESC;
		buf[bp++] = *c;
	}
	return rc;
}

static ViStatus
recvbyte(GpibIntfc *p, ViByte *c, ViUInt32 tmo)
{
	int rc;
	HjAux *ap;
	
	ap = p->aux;
	switch(tmo){
	case VI_TMO_IMMEDIATE: tmo = 1; break;
	case VI_TMO_INFINITE: tmo = 0; break;
	}
	if(ap->rxbufp >= ap->rxbufl){
		ap->rxbufp = 0;
		ap->rxbufl = 0;
		if(ap->rxbufrc < 0){
		error:
			rc = ap->rxbufrc;
			ap->rxbufrc = 0;
			switch(rc){
			case LIBUSB_ERROR_TIMEOUT: return VI_ERROR_TMO;
			default: return VI_ERROR_IO;
			}
		}
		ap->rxbufrc = libusb_bulk_transfer(ap->devh, 0x81, ap->rxbuf, sizeof(ap->rxbuf), &ap->rxbufl, tmo);
		if(ap->rxbufl == 0)
			goto error;
	}
	*c = ap->rxbuf[ap->rxbufp++];
	return VI_SUCCESS;
}

static ViStatus
recvesc(GpibIntfc *p, void *d, ViUInt32 n, int eot, int eos, ViUInt32 *retc, ViUInt32 tmo)
{
	int i, rc, goteot;
	ViByte c;
	
	goteot = 0;
	if(retc != nil) *retc = 0;
	for(i = 0; i < n; i++){
		rc = recvbyte(p, &c, tmo);
		if(rc < 0)
			return rc;
		goteot = eot && c == ChEOI;
		if(c == ChESC || c == ChEOI){
			rc = recvbyte(p, &c, tmo);
			if(rc < 0)
				return rc;
		}
		((ViByte*)d)[i] = c;
		if(retc != nil) (*retc)++;
		if(goteot)
			return VI_SUCCESS;
		if(eos >= 0 && eos == c)
			return VI_SUCCESS_TERM_CHAR;
	}
	return VI_SUCCESS_MAX_CNT;
}

static int
hjgpibstillalive(GpibIntfc *p)
{
	HjAux *ap;
	int cfg;
	
	ap = p->aux;
	return libusb_get_configuration(ap->devh, &cfg) >= 0;
}

static void
hjgpibdestroy(GpibIntfc *p)
{
	HjAux *ap;
	
	ap = p->aux;
	libusb_close(ap->devh);
}

static ViStatus
hjgpiblines(GpibIntfc *p, ViByte *state, ViByte *cap)
{
	char status[] = {ChESC, CmdStatus};
	int rc;
	
	if(cap != nil) *cap = 0xff;
	if(state == nil) return VI_SUCCESS;
	rc = sendraw(p, status, sizeof(status), nil, VI_TMO_INFINITE);
	if(rc < 0) return rc;
	return recvesc(p, state, 1, 0, -1, nil, VI_TMO_INFINITE);
}

static ViStatus
hjgpibread(GpibIntfc *p, void *d, ViUInt32 n, ViUInt32 *retc, int eot, int eos, ViUInt32 tmo)
{
	ViByte cmd[] = {ChESC, eot ? CmdReadEOI1 : CmdReadEOI0, ChESC, CmdReadEOS, eos, eos >= 0 ? 0x14 : 0, ChESC, CmdReadN, n, n >> 8, n >> 16, n >> 24, -1};
	int rc;
	
	rc = sendraw(p, cmd, sizeof(cmd), nil, tmo);
	if(rc < 0) return rc;
	rc = recvesc(p, d, n, eot, eos, retc, tmo);
	if(rc == VI_ERROR_TMO)
		reset(p);
	return rc;
}

static ViStatus
hjgpibwrite(GpibIntfc *p, void *d, ViUInt32 n, ViUInt32 *retc, int eot, ViUInt32 tmo)
{
	return sendesc(p, d, n, eot, retc, tmo);
}

static ViStatus
hjgpibsetatn(GpibIntfc *p, int atn, ViUInt32 tmo)
{
	ViByte cmd[] = {ChESC, 0};

	switch(atn){
	case VI_GPIB_ATN_DEASSERT:
		if((p->state & GPIBIFTALK) != 0)
			cmd[1] = CmdATN1T;
		else
			cmd[1] = CmdATN1L;
		break;
	case VI_GPIB_ATN_ASSERT:
	case VI_GPIB_ATN_ASSERT_IMMEDIATE:
		cmd[1] = CmdATN0;
		break;
	default:
		return VI_ERROR_INV_MODE;
	}
	return sendraw(p, cmd, sizeof(cmd), nil, tmo);
}

static ViStatus
hjgpibsetren(GpibIntfc *p, int ren, ViUInt32 tmo)
{
	ViByte cmd[] = {ChESC, 0};

	cmd[1] = ren ? CmdREN0 : CmdREN1;
	return sendraw(p, cmd, sizeof(cmd), nil, tmo);
}

static ViStatus
hjgpibsendifc(GpibIntfc *p, ViUInt32 tmo)
{
	ViByte cmd[] = {ChESC, CmdIFC};

	return sendraw(p, cmd, sizeof(cmd), nil, tmo);
}


const GpibTab hjgpibtab = {
	.enumerate = hjgpibenumerate,
	.stillalive = hjgpibstillalive,
	.destroy = hjgpibdestroy,
	.read = hjgpibread,
	.write = hjgpibwrite,
	.lines = hjgpiblines,
	.setatn = hjgpibsetatn,
	.setren = hjgpibsetren,
	.sendifc = hjgpibsendifc,
};

