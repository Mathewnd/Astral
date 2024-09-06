ISODIR=$(shell pwd)/iso
ISO=astral.iso
DISKNAME=hdd.img
LIMINEDIR=$(shell pwd)/host-pkgs/limine/usr/local/share/limine/
KERNEL=$(shell pwd)/builds/astral/astral
QEMUFLAGS=-M q35 -cdrom $(ISO) -m 4g -smp cpus=1 -no-shutdown -no-reboot -monitor stdio -debugcon file:/dev/stdout -serial file:/dev/stdout -netdev user,id=net0 -device virtio-net,netdev=net0 -object filter-dump,id=f1,netdev=net0,file=netdump.dat
QEMUDISKFLAGS=-drive file=$(DISKNAME),if=none,id=nvme -device nvme,serial=deadc0ff,drive=nvme -boot order=dc
INITRD=$(shell pwd)/initrds/initrd
DISTROTYPE=full

MINIMALPACKAGES=mlibc bash coreutils init distro-files vim nano mount netd shadow sudo neofetch

.PHONY: all kernel clean clean-kernel iso initrd full minimal disk

all: jinx
	git submodule update --init --recursive
	mkdir -p $(ISODIR)
	make kernel
	make build-$(DISTROTYPE)
	make $(ISO)
	@echo
	@echo "|------------------------------------------------------------------------------------------|"
	@echo "|To regenerate the initrd, run 'make initrd iso'. To generate a disk image, run 'make disk'|"
	@echo "|------------------------------------------------------------------------------------------|"
	@echo

jinx:
	curl https://raw.githubusercontent.com/mintsuki/jinx/99d8151ca5f849e48c38cbe1a232c6a7a1dbc681/jinx > jinx
	chmod +x jinx

iso: $(ISO)

$(ISO): limine.conf liminebg.bmp $(KERNEL) $(INITRD)-$(DISTROTYPE)
	mkdir -p $(ISODIR)
	ln -f $(INITRD)-$(DISTROTYPE) $(ISODIR)/initrd
	cp $(KERNEL) liminebg.bmp limine.conf $(LIMINEDIR)/limine-bios.sys $(LIMINEDIR)/limine-bios-cd.bin $(LIMINEDIR)/limine-uefi-cd.bin $(ISODIR)
	xorriso -as mkisofs -b limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table --efi-boot limine-uefi-cd.bin -efi-boot-part --efi-boot-image --protective-msdos-label $(ISODIR) -o $(ISO)

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
	make clean-kernel
	./jinx clean
	rm jinx
	rm -rf $(ISODIR)
	rm -f $(ISO)
	rm -f $(DISKNAME)
	rm -rf initrds

# ------ run targets ------

run:
	qemu-system-x86_64 $(QEMUFLAGS)

run-gdb:
	qemu-system-x86_64 $(QEMUFLAGS) -S -s

run-kvm:
	qemu-system-x86_64 $(QEMUFLAGS) -enable-kvm -cpu host

run-disk:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUDISKFLAGS)

run-disk-gdb:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUDISKFLAGS) -S -s

run-disk-kvm:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUDISKFLAGS) -enable-kvm -cpu host
