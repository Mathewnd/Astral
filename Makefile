JINX_ARCH=x86_64
JINX_DIR=$(shell pwd)/build-$(JINX_ARCH)
ISODIR=$(JINX_DIR)/iso
IMGDIR=$(JINX_DIR)/img
ISO=$(JINX_DIR)/astral.iso
IMG=$(JINX_DIR)/astral-bootable.img
DISKNAME=$(JINX_DIR)/hdd.img
LIMINEDIR=$(JINX_DIR)/host-pkgs/limine/usr/local/share/limine/
KERNEL=$(JINX_DIR)/builds/astral/astral
QEMUFLAGS=\
	-M q35 \
	-m 2g \
	-smp cpus=4 \
	-no-shutdown \
	-no-reboot \
	-debugcon file:/dev/stdout \
	-serial stdio \
	-netdev user,id=net0 -device virtio-net,netdev=net0 \
	-object filter-dump,id=f1,netdev=net0,file=netdump.dat
QEMUISOFLAGS=-cdrom $(ISO)
QEMUDISKFLAGS=-drive file=$(DISKNAME),if=none,id=nvme -device nvme,serial=deadc0ff,drive=nvme -boot order=dc
QEMUIMGFLAGS=-drive file=$(IMG),if=none,id=usb -device nec-usb-xhci,id=xhci -device usb-storage,bus=xhci.0,drive=usb,removable=on
INITRD=$(JINX_DIR)/initrds/initrd
DISTROTYPE=full
INITRDTYPE=minimal

MINIMALPACKAGES=mlibc bash coreutils init distro-files vim nano mount shadow sudo xbps net-base fastfetch

.PHONY: all kernel clean clean-kernel iso img initrd full minimal disk distro-minimal distro-full

all: $(JINX_DIR)/.astral_ok
	git submodule update --init --recursive
	mkdir -p $(ISODIR)
	make kernel
	make $(ISO)
	@echo
	@echo "|--------------------------------------------------------------|"
	@echo "|       To regenerate the initrd, run 'make initrd iso'.       |"
	@echo "| To generate a bootable USB/HDD image, run 'make initrd img'. |"
	@echo "|         To generate a disk image, run 'make disk'.           |"
	@echo "|--------------------------------------------------------------|"
	@echo

jinx:
	curl https://codeberg.org/Mintsuki/jinx/raw/commit/fba7f3150aa0bd13d27161968b1f441836d06279/jinx > jinx
	chmod +x jinx

$(JINX_DIR)/.astral_ok: jinx
	mkdir -p $(JINX_DIR)
	cd $(JINX_DIR) && \
	../jinx init .. ARCH=$(JINX_ARCH) && \
	touch .astral_ok

iso: $(ISO)
img: $(IMG)

$(ISO): limine.conf liminebg.bmp $(KERNEL) $(INITRD)-$(INITRDTYPE)
	mkdir -p $(ISODIR)
	ln -f $(INITRD)-$(INITRDTYPE) $(ISODIR)/initrd
	cp $(KERNEL) liminebg.bmp limine.conf $(LIMINEDIR)/limine-bios.sys $(LIMINEDIR)/limine-bios-cd.bin $(LIMINEDIR)/limine-uefi-cd.bin $(ISODIR)
	xorriso -as mkisofs -b limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table --efi-boot limine-uefi-cd.bin -efi-boot-part --efi-boot-image --protective-msdos-label $(ISODIR) -o $(ISO)

$(IMG): limine.conf liminebg.bmp $(KERNEL) $(INITRD)-$(INITRDTYPE)
	mkdir -p $(IMGDIR)/EFI/BOOT
	ln -f $(INITRD)-$(INITRDTYPE) $(IMGDIR)/initrd
	cp $(KERNEL) liminebg.bmp limine.conf $(LIMINEDIR)/limine-bios.sys $(IMGDIR)
	cp $(LIMINEDIR)/BOOTIA32.EFI $(IMGDIR)/EFI/BOOT
	cp $(LIMINEDIR)/BOOTX64.EFI $(IMGDIR)/EFI/BOOT
	../genbootimg.sh 200m  $(IMG) $(IMGDIR)

# ------ build targets ------

kernel:
	cd $(JINX_DIR) && \
	rm -f builds/astral.packaged && \
	rm -f builds/astral.built && \
	../jinx build astral

distro-minimal:
	cd $(JINX_DIR) && \
	../jinx build-if-needed $(MINIMALPACKAGES)

distro-full:
	cd $(JINX_DIR) && \
	../jinx build-if-needed '*'

# ------ initrd targets ------

$(INITRD)-full: distro-full
	cd $(JINX_DIR) && \
	../jinx install sysroot \* && \
	mkdir -p initrds && \
	../geninitrd.sh sysroot $(INITRD)-full

$(INITRD)-minimal: distro-minimal
	cd $(JINX_DIR) && \
	../jinx install minimalsysroot $(MINIMALPACKAGES) && \
	mkdir -p initrds && \
	../geninitrd.sh minimalsysroot $(INITRD)-minimal

initrd:
	cd $(JINX_DIR) && \
	rm $(INITRD)-$(INITRDTYPE) && \
	make $(INITRD)-$(INITRDTYPE)

# ------ disk targets ------

disk: disk-$(DISTROTYPE)

disk-full: distro-full
	cd $(JINX_DIR) && \
	../jinx install sysroot \* && \
	../gendisk.sh 8g sysroot $(DISKNAME)

disk-minimal: distro-minimal
	cd $(JINX_DIR) && \
	../jinx install minimalsysroot $(MINIMALPACKAGES) && \
	../gendisk.sh 1g minimalsysroot $(DISKNAME)

# ------ clean targets ------

clean-kernel:
	cd $(JINX_DIR) && \
	find builds/astral/ -name *.d -delete && \
	find builds/astral/ -name *.o -delete && \
	find builds/astral/ -name *.asmo -delete

clean:
	rm -rf jinx
	rm -rf sources
	rm -rf $(JINX_DIR)
# ------ run targets ------

run:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUISOFLAGS)

run-gdb:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUISOFLAGS) -S -s

run-kvm:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUISOFLAGS) -enable-kvm -cpu host,migratable=off

run-disk:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUISOFLAGS) $(QEMUDISKFLAGS)

run-disk-gdb:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUISOFLAGS) $(QEMUDISKFLAGS) -S -s

run-disk-kvm:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUISOFLAGS) $(QEMUDISKFLAGS) -enable-kvm -cpu host,migratable=off -s
	
run-disk-kvm-gdb:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUISOFLAGS) $(QEMUDISKFLAGS) -enable-kvm -cpu host,migratable=off -S -s
	

run-img:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUIMGFLAGS)

run-img-gdb:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUIMGFLAGS) -S -s

run-img-kvm:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUIMGFLAGS) -enable-kvm -cpu host,migratable=off
