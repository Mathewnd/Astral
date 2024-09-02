.phony: all install
CC=x86_64-astral-gcc
LD=x86_64-astral-gcc

all: init poweroff reboot acpid

install:
	mkdir -pv "$(DESTDIR)/usr/sbin"
	cp -v init "$(DESTDIR)/init"
	cp -v poweroff "$(DESTDIR)/usr/sbin/poweroff"
	cp -v reboot "$(DESTDIR)/usr/sbin/reboot"
	cp -v acpid "$(DESTDIR)/usr/sbin/acpid"

init: main.o
	$(LD) $(LDFLAGS) -o $@ $^

poweroff: poweroff.o
	$(LD) $(LDFLAGS) -o $@ $^

reboot: reboot.o
	$(LD) $(LDFLAGS) -o $@ $^

acpid: acpid.o
	$(LD) $(LDFLAGS) -o $@ $^

main.o: main.c
	$(CC) -c $(CFLAGS) -o $@ $<

poweroff.o: poweroff.c
	$(CC) -c $(CFLAGS) -o $@ $<

reboot.o: reboot.c
	$(CC) -c $(CFLAGS) -o $@ $<

acpid.o: acpid.c
	$(CC) -c $(CFLAGS) -o $@ $<
