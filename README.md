# Astral

Astral is a 64 bit operating system with its own kernel written in C for the x86-64 architecture.

![](https://astral-os.org/images/screenshot_2025_11_02.png)

## Features

- SMP capable preemptible kernel
- Networking (TCP, UDP, DHCP)
- POSIX compatibility
- Ports: X.org, wine, fvwm3, gcc, bash, quake, vim and more
- Filesystems: tmpfs, devfs, ext2, fat{12,16,32}
- Block devices: NVMe, virtio-block, AHCI
- Network devices: virtio-net, rtl8169
- USB: xHCI, HID, Hubs
- ACPI: thanks to [uACPI](https://github.com/UltraOS/uACPI), there is ACPI support with proper poweroff, etc.
- Multiple user support
- Package management using xbps

## Current Goals

- Audio
- Installation program
- Fully self hosting

## Building

The build process only needs xorriso, curl, zstd, fakeroot and bsdtar on the host. If you wish to use the ``make img`` option, you will need mtools. All other needed packages will be installed/built on the container.

To build the project, run ``make``. This will create a file named ``astral.iso``

After this, if you wish to create an ext2 disk image, run ``make disk``

## Running
If you built it from source, run ``make run-kvm`` or ``make run-disk-kvm``

There are prebuilt images and instructions at https://astral-os.org/about.html. 
