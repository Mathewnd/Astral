XCC=x86_64-astral-gcc
DESTDIR=/
PREFIX=/usr/

all: imgview 

imgview: main.o
	$(XCC) $(LDFLAGS) -o $@ $<

%.o: %.c
	$(XCC) -c $(CFLAGS) -o $@ $<

install:
	mkdir -p $(DESTDIR)/$(PREFIX)/bin/
	mv imgview $(DESTDIR)/$(PREFIX)/bin/imgview
