CFLAGS=-Wall -Wno-parentheses -Wno-missing-braces -Wno-pointer-to-int-cast -fno-diagnostics-show-caret -fno-diagnostics-color -gstabs -D_WIN32_WINNT=0x600 -I`pwd` `$(PKG_CONFIG) --cflags libusb-1.0`
LDFLAGS=-shared -static-libgcc -Wl,--enable-stdcall-fixup -gstabs
LDLIBS=-lws2_32 `$(PKG_CONFIG) --libs libusb-1.0`
OFILES=\
	sess.o \
	thread.o \
	io.o \
	fmt.o \
	nimpl.o \
	tcpip.o \
	entry.o \
	msg.o \
	gpib.o \
	hjgpib.o \

TARG=visa32

HFILES=visatype.h visa.h dat.h fns.h

all: $(TARG).dll test.exe

$(TARG).dll: $(OFILES) $(TARG).def
	$(CC) $(LDFLAGS) $(OFILES) $(TARG).def $(LDLIBS) -o $@

%.o: %.c $(HFILES)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARG) $(OFILES) test.o test.exe

test.exe: test.o
	$(CC) -gstabs -Wl,--enable-stdcall-fixup -static-libgcc test.o visa32.dll -o test.exe
