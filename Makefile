ISODIR=$(shell pwd)/iso
IMGDIR=$(shell pwd)/img
ISO=astral.iso
IMG=astral-bootable.img
DISKNAME=hdd.img
LIMINEDIR=$(shell pwd)/host-pkgs/limine/usr/local/share/limine/
KERNEL=$(shell pwd)/builds/astral/astral
QEMUFLAGS=-M q35 -m 4g -smp cpus=1 -no-shutdown -no-reboot -monitor stdio -debugcon file:/dev/stdout -serial file:/dev/stdout -netdev user,id=net0 -device virtio-net,netdev=net0 -object filter-dump,id=f1,netdev=net0,file=netdump.dat
QEMUISOFLAGS=-cdrom $(ISO)
QEMUDISKFLAGS=-drive file=$(DISKNAME),if=none,id=nvme -device nvme,serial=deadc0ff,drive=nvme -boot order=dc
QEMUIMGFLAGS=-drive file=$(IMG),if=none,id=usb -device nec-usb-xhci,id=xhci -device usb-storage,bus=xhci.0,drive=usb,removable=on
INITRD=$(shell pwd)/initrds/initrd
DISTROTYPE=full

MINIMALPACKAGES=mlibc bash coreutils init distro-files vim nano mount netd shadow sudo neofetch

.PHONY: all kernel clean clean-kernel iso img initrd full minimal disk

all: jinx
	git submodule update --init --recursive
	mkdir -p $(ISODIR)
	make kernel
	make build-$(DISTROTYPE)
	make $(ISO)
	@echo
	@echo "|--------------------------------------------------------------|"
	@echo "|       To regenerate the initrd, run 'make initrd iso'.       |"
	@echo "| To generate a bootable USB/HDD image, run 'make initrd img'. |"
	@echo "|         To generate a disk image, run 'make disk'.           |"
	@echo "|--------------------------------------------------------------|"
	@echo

jinx:
	curl https://raw.githubusercontent.com/mintsuki/jinx/99d8151ca5f849e48c38cbe1a232c6a7a1dbc681/jinx > jinx
	chmod +x jinx

iso: $(ISO)
img: $(IMG)

$(ISO): limine.conf liminebg.bmp $(KERNEL) $(INITRD)-$(DISTROTYPE)
	mkdir -p $(ISODIR)
	ln -f $(INITRD)-$(DISTROTYPE) $(ISODIR)/initrd
	cp $(KERNEL) liminebg.bmp limine.conf $(LIMINEDIR)/limine-bios.sys $(LIMINEDIR)/limine-bios-cd.bin $(LIMINEDIR)/limine-uefi-cd.bin $(ISODIR)
	xorriso -as mkisofs -b limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table --efi-boot limine-uefi-cd.bin -efi-boot-part --efi-boot-image --protective-msdos-label $(ISODIR) -o $(ISO)

$(IMG): limine.conf liminebg.bmp $(KERNEL) $(INITRD)-$(DISTROTYPE)
	mkdir -p $(IMGDIR)/EFI/BOOT
	ln -f $(INITRD)-$(DISTROTYPE) $(IMGDIR)/initrd
	cp $(KERNEL) liminebg.bmp limine.conf $(LIMINEDIR)/limine-bios.sys $(IMGDIR)
	cp $(LIMINEDIR)/BOOT{IA32,X64}.EFI $(IMGDIR)/EFI/BOOT
	./genbootimg.sh 1G $(IMG) $(IMGDIR)

# ------ build targets ------

build-full:
	./jinx build-all

build-minimal:
	./jinx build $(MINIMALPACKAGES)

kernel:
	rm -f builds/astral.packaged
	rm -f builds/astral.built
	./jinx build astral

# ------ initrd targets ------

$(INITRD)-full:
	./jinx sysroot
	mkdir -p initrds
	./geninitrd.sh sysroot $(INITRD)-full

$(INITRD)-minimal:
	./jinx install minimalsysroot $(MINIMALPACKAGES)
	mkdir -p initrds
	./geninitrd.sh minimalsysroot $(INITRD)-minimal

initrd:
	rm $(INITRD)-$(DISTROTYPE)
	make $(INITRD)-$(DISTROTYPE)

# ------ disk targets ------

disk: disk-$(DISTROTYPE)

disk-full:
	./jinx sysroot
	./gendisk.sh 4g sysroot $(DISKNAME)

disk-minimal:
	./jinx install minimalsysroot $(MINIMALPACKAGES)
	./gendisk.sh 1g minimalsysroot $(DISKNAME)

# ------ clean targets ------

clean-kernel:
	find builds/astral/ -name *.o -delete
	find builds/astral/ -name *.asmo -delete

clean:
	make -i clean-kernel
	./jinx clean
	rm jinx
	rm -rf $(ISODIR)
	rm -f $(ISO)
	rm -f $(DISKNAME)
	rm -rf initrds

# ------ run targets ------

run:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUISOFLAGS)

run-gdb:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUISOFLAGS) -S -s

run-kvm:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUISOFLAGS) -enable-kvm -cpu host

run-disk:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUISOFLAGS) $(QEMUDISKFLAGS)

run-disk-gdb:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUISOFLAGS) $(QEMUDISKFLAGS) -S -s

run-disk-kvm:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUISOFLAGS) $(QEMUDISKFLAGS) -enable-kvm -cpu host

run-img:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUIMGFLAGS)

run-img-gdb:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUIMGFLAGS) -S -s

run-img-kvm:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUIMGFLAGS) -enable-kvm -cpu host
