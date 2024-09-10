# Astral

Astral is a 64 bit operating system with its own kernel written in C for the x86-64 architecture.

![](https://raw.githubusercontent.com/Mathewnd/Astral/rewrite/screenshots/console.png)
![](https://raw.githubusercontent.com/Mathewnd/Astral/rewrite/screenshots/fvwm3.png)

## Features

- SMP capable preemptible kernel
- Networking (TCP, UDP, DHCP)
- POSIX compatibility
- Ports: X.org, fvwm3, gcc, bash, quake, vim and more
- Filesystems: tmpfs, devfs, ext2
- Block devices: NVMe, virtio-block
- Network devices: virtio-net
- ACPI: thanks to [uACPI](https://github.com/UltraOS/uACPI), there is ACPI support with proper poweroff, etc.
- Multiple user support

## Current Goals

- FAT filesystems
- Audio
- Installation program
- Fully self hosting

## Building

The build process only needs xorriso, curl, zstd, fakeroot and bsdtar on the host. If you wish to use the ``make img`` option, you will need mtools. All other needed packages will be installed/built on the container.

To build the project, run ``make``. This will create a file named ``astral.iso``

After this, if you wish to create an ext2 disk image, run ``make disk``

## Running
You can download the latest release from github and run it in the virtual machine of your liking.
For QEMU the following command is recommended:
``qemu-system-x86_64 -enable-kvm -M q35 -m 2g -cdrom astral.iso``

There are a few targets in the makefile to run Astral with qemu when built from source:

``make run``

``make run-kvm``

``make run-disk`` (if you built the disk image)

``make run-disk-kvm`` (if you built the disk image)
