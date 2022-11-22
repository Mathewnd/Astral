.phony: all kernel initrd rebuildkernel minimaliso minimalinitrd
all: jinx
	LDFLAGS="" CFLAGS="" ./jinx build-all
	make fullinitrd
	make sysdisk.iso
	make nvme.img
	
rebuildkernel:
	LDFLAGS="" CFLAGS="" ./jinx rebuild astral
	make sysdisk.iso
jinx:
	curl https://raw.githubusercontent.com/mintsuki/jinx/trunk/jinx > jinx
	chmod +x jinx

minimaliso:
	make minimalinitrd
	make sysdisk.iso


XCC=x86_64-elf-gcc
TARGET=x86_64
rwildcard=$(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))
KERNELDEPS=$(call rwildcard,bin,*.o)
KERNELSRCDEPS=$(call rwildcard,src,*.c)
INCLUDEDIR=$(SRCDIR)/include
ARCHINCLUDE=$(SRCDIR)/arch/$(TARGET)/include
KERNELCONFIG=
KERNEL=$(ISO)/kernel
INITRD=$(ISO)/initrd
CFLAGS=-c -ffreestanding -mcmodel=kernel -O2 -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -nostdlib -I$(INCLUDEDIR) -I$(ARCHINCLUDE) --debug $(KERNELCONFIG)
LDFLAGS=-ffreestanding -nostdlib -lgcc -Wl,-T,kernel.ld -debug
ISO=$(PWD)/boot/$(TARGET)/iso
SRCDIR=$(PWD)/src/
OBJDIR=$(PWD)/bin/

fullinitrd:
	./jinx sysroot
	rm -f $(INITRD)
	cd sysroot; tar -cf $(INITRD) *

minimalinitrd:
	./jinx install minsysroot bash mlibc coreutils distro-files init mlibc-headers nano
	rm -f $(INITRD)
	cd minsysroot; tar -cf $(INITRD) *

#TODO make it so we don't need to rebuild everything when a header is changed

export OBJDIR SRCDIR LDFLAGS CFLAGS INCLUDEDIR TARGET XCC

kernel: $(KERNEL)

srcdir:
	$(MAKE) -C src

$(KERNEL): srcdir $(KERNELSRCDEPS) kernel.ld
	mkdir -p $(ISO)
	$(XCC) $(LDFLAGS) -o $@ $(call rwildcard,bin,*.o)

sysdisk.iso: $(ISO)
	mkdir -p $(ISO)
	cp liminebg.bmp limine.cfg limine/limine.sys limine/limine-cd.bin limine/limine-cd-efi.bin $(ISO)
	xorriso -as mkisofs -b limine-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table --efi-boot limine-cd-efi.bin -efi-boot-part --efi-boot-image --protective-msdos-label $(ISO) -o sysdisk.iso

nvme.img:
	qemu-img create nvme.img 4G -f qcow2

QEMUFLAGS=-cdrom sysdisk.iso -m 8G -smp cpus=6 -drive file=nvme.img,if=none,id=nvm -device nvme,serial=deadc0ff,drive=nvm
run:
	qemu-system-x86_64 $(QEMUFLAGS) -debugcon stdio

run-kvm:
	qemu-system-x86_64 $(QEMUFLAGS) -enable-kvm -debugcon stdio

test:
	qemu-system-x86_64 $(QEMUFLAGS) -monitor stdio -cdrom -d int -no-reboot -no-shutdown

test-kvm:
	qemu-system-x86_64 $(QEMUFLAGS) -monitor stdio -no-reboot -no-shutdown -enable-kvm

kclean:

	rm $(call rwildcard,src,*.o) $(KERNELDEPS)

clean:
	./jinx clean
	rm -f jinx
	rm $(call rwildcard,src,*.o) $(KERNELDEPS)

	
