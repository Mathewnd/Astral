ISODIR=$(shell pwd)/iso
ISO=astral.iso
LIMINEDIR=$(shell pwd)/host-pkgs/limine/usr/local/share/limine/
KERNEL=$(shell pwd)/pkgs/astral/boot/astral
QEMUFLAGS=-cdrom $(ISO) -m 2G -smp cpus=2 -no-shutdown -no-reboot -monitor stdio -debugcon file:/dev/stdout -serial file:/dev/stdout # hacky but works :')
INITRD=$(shell pwd)/iso/initrd
DISTROTYPE=full

.PHONY: all kernel clean clean-kernel iso initrd full minimal

all: jinx
	make kernel
	./jinx build-all
	make $(ISO)

jinx:
	curl https://raw.githubusercontent.com/mintsuki/jinx/trunk/jinx > jinx
	chmod +x jinx

$(ISO): limine.cfg liminebg.bmp $(KERNEL) initrd
	mkdir -p $(ISODIR)
	cp $(KERNEL) liminebg.bmp limine.cfg $(LIMINEDIR)/limine-bios.sys $(LIMINEDIR)/limine-bios-cd.bin $(LIMINEDIR)/limine-uefi-cd.bin $(ISODIR)
	xorriso -as mkisofs -b limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table --efi-boot limine-uefi-cd.bin -efi-boot-part --efi-boot-image --protective-msdos-label $(ISODIR) -o $(ISO)

initrd:
	mkdir -p $(ISODIR)
	make $(DISTROTYPE)

full:
	./jinx sysroot
	cd sysroot; tar --format=ustar -cf $(INITRD) *

minimal:
	./jinx install minimalsysroot mlibc bash coreutils init distro-files vim nano mount
	cd minimalsysroot; tar --format=ustar -cf $(INITRD) *

kernel:
	rm -f builds/astral.configured
	rm -f builds/astral.installed
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
