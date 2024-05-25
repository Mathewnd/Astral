# Astral

Astral is a 64 bit operating system with its own kernel written in C for the x86-64 architecture.

![](https://raw.githubusercontent.com/Mathewnd/Astral/rewrite/screenshots/console.png)
![](https://raw.githubusercontent.com/Mathewnd/Astral/rewrite/screenshots/fvwm3.png)

## Features

- SMP capable preemptible kernel
- Networking (UDP, DHCP)
- POSIX compatibility
- Ports: X.org, fvwm3, gcc, bash, quake, vim and more
- Filesystems: tmpfs, devfs, ext2
- Block devices: NVMe, virtio-block
- Network devices: virtio-net

## Current Goals

- TCP
- Fat32

## Building

The build process only needs xorriso, curl and bsdtar on the host. All other needed packages will be installed/built on the container.

To build the project, run ``make``. This will create a file named ``astral.iso``

## Running
You can download the latest release from github and run it in the virtual machine of your liking.
For QEMU the following command is recommended:
``qemu-system-x86_64 -enable-kvm -M q35 -m 2g -cdrom astral.iso``

There are a few targets in the makefile to run Astral with qemu when built from source:

``make run``

``make run-kvm``

VirtualBox-specific instructions:

Set 1 CPU for the VM\
After you've created a new VM from the iso file:\
`vboxmanage list vms`\
`vboxmanage modifyvm <name of Astral VM> --hpet on`\
Then select "Astral (initrd)" from the boot menu
