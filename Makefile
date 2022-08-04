CC=x86_64-elf-gcc
TARGET=x86_64
SRCDIR=$(PWD)/src/
OBJDIR=$(PWD)/bin/
ISO=$(PWD)/boot/$(TARGET)/iso
KERNEL=$(ISO)/kernel
INITRD=$(ISO)/initrd
rwildcard=$(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))
KERNELDEPS=$(call rwildcard,bin,*.o)
KERNELSRCDEPS=$(call rwildcard,src,*.c)
INCLUDEDIR=$(SRCDIR)/include
ARCHINCLUDE=$(SRCDIR)/arch/$(TARGET)/include
CFLAGS=-c -ffreestanding -mcmodel=kernel -O2 -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -nostdlib -I$(INCLUDEDIR) -I$(ARCHINCLUDE) --debug
LDFLAGS=-ffreestanding -nostdlib -lgcc -Wl,-T,kernel.ld -debug


#TODO make it so we don't need to rebuild everything when a header is changed

export OBJDIR SRCDIR LDFLAGS CFLAGS INCLUDEDIR TARGET CC

.phony: all
all: $(KERNEL)

srcdir:
	$(MAKE) -C src

$(KERNEL): srcdir $(KERNELSRCDEPS) kernel.ld
	mkdir -p $(ISO)
	$(CC) $(LDFLAGS) -o $@ $(call rwildcard,bin,*.o)

$(INITRD): $(call rwildcard,initrd,*)
	cd initrd;tar -cf $(INITRD) *

sysdisk.iso: $(ISO) $(INITRD)
	cp liminebg.bmp limine.cfg limine/limine.sys limine/limine-cd.bin limine/limine-cd-efi.bin $(ISO)
	xorriso -as mkisofs -b limine-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table --efi-boot limine-cd-efi.bin -efi-boot-part --efi-boot-image --protective-msdos-label $(ISO) -o sysdisk.iso
	

test: $(KERNEL) sysdisk.iso
	qemu-system-x86_64 -monitor stdio -cdrom sysdisk.iso -d int -no-reboot -no-shutdown -m 8G -smp cpus=6

clean:
	rm $(call rwildcard,src,*.o) $(KERNELDEPS)
