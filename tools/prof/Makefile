.phony: all install
CC=x86_64-astral-gcc
LD=x86_64-astral-gcc
all: astral-prof

install:
	mkdir -p "$(DESTDIR)/$(PREFIX)/bin/"
	cp astral-prof "$(DESTDIR)/$(PREFIX)/bin/astral-prof"

astral-prof: main.o
	$(LD) $(LDFLAGS) -g -o $@ $^

main.o: main.c
	$(CC) -c $(CFLAGS) -g -o $@ $<
