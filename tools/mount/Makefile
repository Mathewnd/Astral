.phony: all install
CC=x86_64-astral-gcc
LD=x86_64-astral-gcc
all: mount

install:
	mkdir -p "$(DESTDIR)/$(PREFIX)/bin/"
	cp mount "$(DESTDIR)/$(PREFIX)/bin/mount"

mount: main.o
	$(LD) $(LDFLAGS) -o $@ $^

main.o: main.c
	$(CC) -c $(CFLAGS) -o $@ $<
