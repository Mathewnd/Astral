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
	-M q35,i8042=off \
	-m 8g \
	-smp cpus=1 \
	-debugcon file:/dev/stdout \
	-netdev user,id=net0 -device virtio-net,netdev=net0 \
	-device qemu-xhci,id=input-xhci \
	-device usb-hub,bus=input-xhci.0,port=1 \
	-device usb-kbd,bus=input-xhci.0,port=1.1 \
	-device usb-hub,bus=input-xhci.0,port=2 \
	-device usb-tablet,bus=input-xhci.0,port=2.1 \
	-object filter-dump,id=f1,netdev=net0,file=netdump.dat
QEMUISOFLAGS=-cdrom $(ISO)
QEMUDISKFLAGS=-drive file=$(DISKNAME),if=none,id=disk \
	      -device nvme,drive=disk,serial=0xdeadbeef \
	      -boot dc
QEMUIMGFLAGS=-drive file=$(IMG),if=none,id=usb -device nec-usb-xhci,id=xhci -device usb-storage,bus=xhci.0,drive=usb,removable=on
INITRD=$(JINX_DIR)/initrds/initrd
DISTROTYPE=full
INITRDTYPE=minimal

MINIMALPACKAGES=mlibc bash coreutils openrc distro-files vim nano mount shadow sudo xbps net-base fastfetch limine dosfstools e2fsprogs parted netinfo systrace findutils sed

.PHONY: all kernel clean clean-kernel iso img initrd full minimal disk distro-minimal distro-full download

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

jinx/jinx:
	git submodule update --init --recursive jinx

$(JINX_DIR)/.astral_ok: jinx/jinx
	mkdir -p $(JINX_DIR)
	cd $(JINX_DIR) && \
	../jinx/jinx init .. ARCH=$(JINX_ARCH) && \
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
	cd $(JINX_DIR) && ../genbootimg.sh 1g $(IMG) $(IMGDIR)

# ------ build targets ------

kernel:
	cd $(JINX_DIR) && \
	rm -f builds/astral.packaged && \
	rm -f builds/astral.built && \
	../jinx/jinx build astral

distro-minimal:
	cd $(JINX_DIR) && \
	../jinx/jinx update $(MINIMALPACKAGES)

distro-full:
	cd $(JINX_DIR) && \
	../jinx/jinx update '*'

# ------ initrd targets ------

$(INITRD)-full: distro-full
	cd $(JINX_DIR) && \
	../jinx/jinx install sysroot \* && \
	mkdir -p initrds && \
	../geninitrd.sh sysroot $(INITRD)-full

$(INITRD)-minimal: distro-minimal
	cd $(JINX_DIR) && \
	../jinx/jinx install minimalsysroot $(MINIMALPACKAGES) && \
	mkdir -p initrds && \
	../geninitrd.sh minimalsysroot $(INITRD)-minimal

initrd:
	cd $(JINX_DIR) && \
	rm $(INITRD)-$(INITRDTYPE) && \
	make $(INITRD)-$(INITRDTYPE)

# ------ download targets ------

download-full:
	cd $(JINX_DIR) && \
	../jinx/jinx download '*'
	
download-minimal:
	cd $(JINX_DIR) && \
	../jinx/jinx download $(MINIMALPACKAGES)

# ------ disk targets ------

disk: disk-$(DISTROTYPE)

disk-full: distro-full
	cd $(JINX_DIR) && \
	../jinx/jinx install sysroot \* && \
	../gendisk.sh 7g sysroot $(DISKNAME)

disk-minimal: distro-minimal
	cd $(JINX_DIR) && \
	../jinx/jinx install minimalsysroot $(MINIMALPACKAGES) && \
	../gendisk.sh 1900m minimalsysroot $(DISKNAME)

# ------ download targets ------

download: $(JINX_DIR)/.astral_ok
	cd $(JINX_DIR) && \
	../jinx/jinx download '*'

# ------ clean targets ------

clean-kernel:
	cd $(JINX_DIR) && \
	find builds/astral/ -name *.d -delete && \
	find builds/astral/ -name *.o -delete && \
	find builds/astral/ -name *.asmo -delete

clean:
	rm -rf sources
	rm -rf $(JINX_DIR)
# ------ run targets ------

run:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUISOFLAGS) -serial stdio

run-gdb:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUISOFLAGS) -S -s -serial stdio

run-kvm:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUISOFLAGS) -enable-kvm -cpu host,migratable=off -serial stdio

run-disk:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUISOFLAGS) $(QEMUDISKFLAGS) -serial stdio

run-disk-gdb:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUISOFLAGS) $(QEMUDISKFLAGS) -S -s -serial stdio

run-disk-kvm:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUISOFLAGS) $(QEMUDISKFLAGS) -enable-kvm -cpu host,migratable=off -s -serial stdio

run-disk-kvm-prof:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUISOFLAGS) $(QEMUDISKFLAGS) -enable-kvm -cpu host,migratable=off -s -serial file:profiler.out
	
run-disk-kvm-gdb:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUISOFLAGS) $(QEMUDISKFLAGS) -enable-kvm -cpu host,migratable=off -S -s -serial stdio
	
run-img:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUIMGFLAGS) -serial stdio

run-img-gdb:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUIMGFLAGS) -S -s -serial stdio

run-img-kvm:
	qemu-system-x86_64 $(QEMUFLAGS) $(QEMUIMGFLAGS) -enable-kvm -cpu host,migratable=off -serial stdio
