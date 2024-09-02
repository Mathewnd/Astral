.phony: all install
CC=x86_64-astral-gcc
LD=x86_64-astral-gcc

all: init poweroff

install:
	mkdir -pv "$(DESTDIR)/usr/sbin"
	cp -v init "$(DESTDIR)/init"
	cp -v poweroff "$(DESTDIR)/usr/sbin/poweroff"

init: main.o
	$(LD) $(LDFLAGS) -o $@ $^

poweroff: poweroff.o
	$(LD) $(LDFLAGS) -o $@ $^

main.o: main.c
	$(CC) -c $(CFLAGS) -o $@ $<

poweroff.o: poweroff.c
	$(CC) -c $(CFLAGS) -o $@ $<
