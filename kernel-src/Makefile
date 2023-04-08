CSOURCES= $(shell find -name *.c)
ASMSOURCES= $(shell find -name *.asm)
OBJS=$(CSOURCES:.c=.o) $(ASMSOURCES:.asm=.asmo)
TARGET=x86-64
INCDIR=$(shell pwd)/include
ARCHDIR=$(shell pwd)/arch/$(TARGET)/
LDFLAGS=-ffreestanding -nostdlib -lgcc -Wl,-T,"$(ARCHDIR)/kernel.ld" -debug
CFLAGS=-ffreestanding -mcmodel=kernel -O2 -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -nostdlib -I "${INCDIR}"
ASFLAGS=-felf64

astral:	$(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.asmo: %.asm
	nasm $(ASFLAGS) -o $@ $<
