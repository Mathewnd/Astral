ISODIR=$(shell pwd)/iso
ISO=astral.iso
LIMINEDIR=$(shell pwd)/host-pkgs/limine/usr/local/share/limine/
KERNEL=pkgs/astral/boot/astral

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
	rm builds/astral.configured
	rm builds/astral.installed
	rm builds/astral.built
	./jinx build astral

clean-kernel:
	find kernel-src -name *.o -delete

clean:
	make clean-kernel
	./jinx clean
	rm jinx
