ISODIR=$(shell pwd)/iso
ISO=astral.iso
LIMINEDIR=$(shell pwd)/host-pkgs/limine/usr/local/share/limine/
KERNEL=$(shell pwd)/pkgs/astral/boot/astral
QEMUFLAGS=-cdrom $(ISO) -m 2G -smp cpus=2 -monitor stdio -debugcon file:/dev/stdout -serial file:/dev/stdout # hacky but works :')

.PHONY: all kernel clean clean-kernel iso

all: jinx
	make kernel
	./jinx build-all
	make $(ISO)

jinx:
	curl https://raw.githubusercontent.com/mintsuki/jinx/trunk/jinx > jinx
	chmod +x jinx

$(ISO): limine.cfg liminebg.bmp $(KERNEL)
	mkdir -p $(ISODIR)
	cp $(KERNEL) liminebg.bmp limine.cfg $(LIMINEDIR)/limine.sys $(LIMINEDIR)/limine-cd.bin $(LIMINEDIR)/limine-cd-efi.bin $(ISODIR)
	xorriso -as mkisofs -b limine-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table --efi-boot limine-cd-efi.bin -efi-boot-part --efi-boot-image --protective-msdos-label $(ISODIR) -o $(ISO)

kernel:
	rm -f builds/astral.configured
	rm -f builds/astral.installed
	rm -f builds/astral.built
	./jinx build astral

clean-kernel:
	find builds/astral/ -name *.o -delete

clean:
	make clean-kernel
	./jinx clean
	rm jinx

run:
	qemu-system-x86_64 $(QEMUFLAGS)

run-kvm:
	qemu-system-x86_64 $(QEMUFLAGS) -enable-kvm -cpu host
