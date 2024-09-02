ISODIR=$(shell pwd)/iso
ISO=astral.iso
LIMINEDIR=$(shell pwd)/host-pkgs/limine/usr/local/share/limine/
KERNEL=$(shell pwd)/pkgs/astral/boot/astral
QEMUFLAGS=-M q35 -cdrom $(ISO) -m 2G -smp cpus=1 -no-shutdown -no-reboot -monitor stdio -debugcon file:/dev/stdout -serial file:/dev/stdout # hacky but works :')
INITRD=$(shell pwd)/iso/initrd
DISTROTYPE=full

MINIMALPACKAGES=mlibc bash coreutils init distro-files vim nano mount netd

.PHONY: all kernel clean clean-kernel iso initrd full minimal

all: jinx
	git submodule update --init --recursive
	make kernel
	make build-$(DISTROTYPE)
	make $(ISO)

jinx:
	curl https://raw.githubusercontent.com/mintsuki/jinx/99d8151ca5f849e48c38cbe1a232c6a7a1dbc681/jinx > jinx
	chmod +x jinx

$(ISO): limine.cfg liminebg.bmp $(KERNEL) initrd
	mkdir -p $(ISODIR)
	cp $(KERNEL) liminebg.bmp limine.cfg $(LIMINEDIR)/limine-bios.sys $(LIMINEDIR)/limine-bios-cd.bin $(LIMINEDIR)/limine-uefi-cd.bin $(ISODIR)
	xorriso -as mkisofs -b limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table --efi-boot limine-uefi-cd.bin -efi-boot-part --efi-boot-image --protective-msdos-label $(ISODIR) -o $(ISO)

initrd:
	mkdir -p $(ISODIR)
	make $(DISTROTYPE)

build-full:
	./jinx build-all

build-minimal:
	./jinx build $(MINIMALPACKAGES)

full:
	./jinx sysroot
	cd sysroot; tar --format=ustar -cf $(INITRD) *

minimal:
	./jinx install minimalsysroot $(MINIMALPACKAGES)
	cd minimalsysroot; tar --format=ustar -cf $(INITRD) *

kernel:
	rm -f builds/astral.packaged
	rm -f builds/astral.built
	./jinx build astral

clean-kernel:
	find builds/astral/ -name *.o -delete
	find builds/astral/ -name *.asmo -delete

clean:
	make clean-kernel
	./jinx clean
	rm jinx

run:
	qemu-system-x86_64 $(QEMUFLAGS)

run-gdb:
	qemu-system-x86_64 $(QEMUFLAGS) -S -s

run-kvm:
	qemu-system-x86_64 $(QEMUFLAGS) -enable-kvm -cpu host
