.phony: all install
CC=x86_64-astral-gcc
LD=x86_64-astral-gcc
all: netd

install:
	mkdir -p "$(DESTDIR)/$(PREFIX)/sbin/"
	cp netd "$(DESTDIR)/$(PREFIX)/sbin/netd"

netd: main.o
	$(LD) $(LDFLAGS) -g -o $@ $^

main.o: main.c
	$(CC) -c $(CFLAGS) -g -o $@ $<
