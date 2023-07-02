CSOURCES= $(shell find -name *.c)
ASMSOURCES= $(shell find -name *.asm)
OBJS=$(CSOURCES:.c=.o) $(ASMSOURCES:.asm=.asmo)
TARGET=x86-64
INCDIR=$(shell pwd)/include
ARCHINCDIR=$(shell pwd)/include/$(TARGET)/
ARCHDIR=$(shell pwd)/arch/$(TARGET)/
LDFLAGS=-ffreestanding -nostdlib -lgcc -Wl,-T,"$(ARCHDIR)/kernel.ld" -g
# required for mpaland printf so it doesn't compile with floating point support, which results in build failures
PRINTFOPTS=-DPRINTF_DISABLE_SUPPORT_FLOAT
# flanterm includes break if the headers are moved into the include dir in the tree
FLANTERMINCDIR=$(shell pwd)/flanterm
CFLAGS=-g -ffreestanding -mcmodel=kernel -O2 -mno-red-zone -mgeneral-regs-only -mno-mmx -mno-sse -mno-sse2 -nostdlib -Wall -I "${INCDIR}" -I "$(ARCHINCDIR)" -I "$(FLANTERMINCDIR)" $(PRINTFOPTS)
ASFLAGS=-felf64

astral:	$(OBJS)
	@echo [LD] $@
	@$(LD) $(LDFLAGS) -o $@ $^

%.o: %.c
	@echo [CC] $@
	@$(CC) $(CFLAGS) -c -o $@ $<

%.asmo: %.asm
	@echo [AS] $@
	@nasm $(ASFLAGS) -o $@ $<
