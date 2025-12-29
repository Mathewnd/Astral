.phony: all install
CC=x86_64-astral-gcc
LD=x86_64-astral-gcc
all: systrace

install:
	mkdir -p "$(DESTDIR)/$(PREFIX)/sbin/"
	cp systrace "$(DESTDIR)/$(PREFIX)/sbin/systrace"

systrace: main.o
	$(LD) $(LDFLAGS) -g -o $@ $^

main.o: main.c
	$(CC) -c $(CFLAGS) -g -o $@ $<
