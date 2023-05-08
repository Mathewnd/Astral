.phony: all install
CC=x86_64-astral-gcc
LD=x86_64-astral-gcc
all: init

install:
	cp init "$(DESTDIR)/init"

init: main.o
	$(LD) $(LDFLAGS) -o $@ $^

main.o: main.c
	$(CC) -c $(CFLAGS) -o $@ $< 
