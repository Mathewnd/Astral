XCC=x86_64-astral-gcc

all: init

init: main.o
	$(XCC) $(LDFLAGS) -o $@ $<

%.o: %.c
	$(XCC) -c $(CFLAGS) -o $@ $<

install:
	mkdir -p $(DESTDIR)/sbin/
	mv init $(DESTDIR)/sbin/init
