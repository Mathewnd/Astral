# Astral

Astral is a 64 bit hobbyist operating system for the x86-64 architecture written in C.

It has ports such as mlibc, doomgeneric, bash, nasm, binutils and the coreutils.

![](https://raw.githubusercontent.com/Mathewnd/Astral/main/gh-stuff/screenie.png)

## Building

The build process only needs xorriso and curl on the host. All other needed packages will be installed/built on the container.

To build an ISO, run ``make``. This will create a file named ``sysdisk.iso``

## Testing

There are a few targets in the makefile to run Astral with qemu:

``make run``

``make run-kvm``

If you wish to use the qemu monitor and get interrupt information, use the test targets:

``make test``

``make test-kvm``

