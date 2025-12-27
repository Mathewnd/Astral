CSOURCES= $(shell find -name "*.c" -not -path "*/uacpi/tests/*")
ASMSOURCES= $(shell find -name "*.asm")
OBJS=$(CSOURCES:.c=.o) $(ASMSOURCES:.asm=.asmo)
INCDIR=$(shell pwd)/include
ARCHINCDIR=$(shell pwd)/include/$(TARGET)/
ARCHDIR=$(shell pwd)/arch/$(TARGET)/
LDFLAGS=-ffreestanding -nostdlib -lgcc -Wl,-T,"$(ARCHDIR)/kernel.ld" -g
FLANTERMOPTS=-DFLANTERM_FB_DISABLE_BUMP_ALLOC
# required for mpaland printf so it doesn't compile with floating point support, which results in build failures
PRINTFOPTS=-DPRINTF_DISABLE_SUPPORT_FLOAT
# flanterm includes break if the headers are moved into the include dir in the tree
FLANTERMINCDIR=$(shell pwd)/flanterm
UACPIINCDIR=$(shell pwd)/io/acpi/uacpi/include
UACPIOPTS=-DUACPI_OVERRIDE_LIBC -DUACPI_KERNEL_INITIALIZATION -DUACPI_NATIVE_ALLOC_ZEROED
KERNELCONFIG=#-DX86_64_ENABLE_E9 -DENABLE_PROFILING #-DSYSCALL_LOGGING
CFLAGS=-g -ffreestanding -mcmodel=kernel -O2 -mno-red-zone -mgeneral-regs-only -mno-mmx -mno-sse -mno-sse2 -nostdlib -Wall -MMD -I "$(INCDIR)" -I "$(ARCHINCDIR)" -I "$(FLANTERMINCDIR)" -I "$(UACPIINCDIR)" $(UACPIOPTS) $(PRINTFOPTS) $(KERNELCONFIG) $(FLANTERMOPTS) -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer -std=gnu17 -Wno-error=implicit-function-declaration
ASFLAGS=-felf64
DEPS=$(CSOURCES:.c=.d)

.PHONY: all
all:
	# build build.h file
	echo "#ifndef _BUILD_H" > include/build.h 
	echo "#define _BUILD_H" >> include/build.h 
	echo "	#define BUILD_DATE \"$(shell date -u)\"" >> include/build.h
	echo "	#define BUILD_TARGET \"$(JINX_ARCH)\"" >> include/build.h
	echo "#endif" >> include/build.h 
	make astral

astral:	$(OBJS)
	@echo [LD] $@
	@$(LD) $(LDFLAGS) -o $@ $^

%.o: %.c Makefile
	@echo [CC] $@
	@$(CC) $(CFLAGS) -c -o $@ $<

%.asmo: %.asm Makefile
	@echo [AS] $@
	@nasm $(ASFLAGS) -o $@ $<

-include $(DEPS)
