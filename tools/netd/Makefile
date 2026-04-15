.phony: all install
CC=x86_64-astral-gcc
LD=x86_64-astral-gcc
all: netd hostname

install:
	mkdir -p "$(DESTDIR)/$(PREFIX)/sbin/"
	mkdir -p "$(DESTDIR)/$(PREFIX)/bin/"
	cp netd "$(DESTDIR)/$(PREFIX)/sbin/netd"
	cp hostname "$(DESTDIR)/$(PREFIX)/bin/hostname"

netd: main.o
	$(LD) $(LDFLAGS) -g -o $@ $^

hostname: hostname.o
	$(LD) $(LDFLAGS) -g -o $@ $^

main.o: main.c
	$(CC) -c $(CFLAGS) -g -o $@ $<

hostname.o: hostname.c
	$(CC) -c $(CFLAGS) -g -o $@ $<
