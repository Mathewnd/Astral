# Astral

Astral is a 64 bit operating system with its own kernel written in C for the x86-64 architecture.

![](https://raw.githubusercontent.com/Mathewnd/Astral/rewrite/screenshots/console.png)

## Features

- SMP capable preemptible kernel
- Networking (UDP, DHCP)
- POSIX compatibility
- Ports: mlibc, gcc, bash, doomgeneric, vim, nano and more
- Filesystems: tmpfs, devfs, ext2
- Block devices: NVMe, virtio-block
- Network devices: virtio-net

## Current Goals

- TCP
- X.org port
- Quake

## Building

The build process only needs xorriso and curl on the host. All other needed packages will be installed/built on the container.

To build the project, run ``make``. This will create a file named ``astral.iso``

## Running

There are a few targets in the makefile to run Astral with qemu:

``make run``

``make run-kvm``

VirtualBox-specific instructions:

Set 1 CPU for the VM\
After you've created a new VM from the iso file:\
`vboxmanage list vms`\
`vboxmanage modifyvm <name of Astral VM> --hpet on`\
Then select "Astral (initrd)" from the boot menu
