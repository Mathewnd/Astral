.phony: all install
CC=x86_64-astral-gcc
LD=x86_64-astral-gcc
all: netinfo

install:
	mkdir -p "$(DESTDIR)/$(PREFIX)/sbin/"
	cp netinfo "$(DESTDIR)/$(PREFIX)/sbin/netinfo"

netinfo: main.o
	$(LD) $(LDFLAGS) -g -o $@ $^

main.o: main.c
	$(CC) -c $(CFLAGS) -g -o $@ $<
