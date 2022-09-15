# Astral

Astral is a 64 bit operating system for the x86-64 architecture written in C.

As of now it has a few ports like bash and mlibc.

## Building

The build process needs xorriso and curl on the host.

To build the ISO, run ``make``. This will create a file named ``sysdisk.iso``

## Testing

There are a few targets in the makefile to run Astral with qemu:

``make run``

``make run-kvm``

If you wish to use the qemu monitor, use the test targets:

``make test``

``make test-kvm``
