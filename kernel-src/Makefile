CSOURCES= $(shell find -name *.c)
ASMSOURCES= $(shell find -name *.asm)
OBJS=$(CSOURCES:.c=.o) $(ASMSOURCES:.asm=.asmo)
TARGET=x86-64
INCDIR=$(shell pwd)/include
ARCHDIR=$(shell pwd)/arch/$(TARGET)/
LDFLAGS=-ffreestanding -nostdlib -lgcc -Wl,-T,"$(ARCHDIR)/kernel.ld" -debug
# required for mpaland printf so it doesn't compile with floating point support, which results in build failures
PRINTFOPTS=-DPRINTF_DISABLE_SUPPORT_FLOAT
CFLAGS=-ffreestanding -mcmodel=kernel -O2 -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -nostdlib -Wall -I "${INCDIR}" $(PRINTFOPTS)
ASFLAGS=-felf64

astral:	$(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.asmo: %.asm
	nasm $(ASFLAGS) -o $@ $<
