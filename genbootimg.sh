#!/usr/bin/bash

if [ $# != 3 ]
then
	echo "usage: $0 size diskname"
	exit 1
fi

if [ -z $1 ]
then
	echo "please pass a non empty size"
	exit 1
fi

if [ -z $2 ]
then
	echo "please pass a non empty disk name"
	exit 1
fi

if [ -z $3 ]
then
	echo "please pass a non empty imgdir"
	exit 1
fi


# most of the below shamelessly stolen from
# https://github.com/limine-bootloader/limine-c-template/blob/trunk/GNUmakefile
# at commit 7310a9b098731cbd4242ed82f5c9b9c60ae14919

rm -f "$2"
truncate -s "$1" "$2"
sgdisk "$2" -n 1:2048 -t 1:ef00
host-pkgs/limine/usr/local/bin/limine bios-install "$2"
mformat -i "$2@@1M"
mmd -i "$2@@1M" ::/EFI ::/EFI/BOOT ::/boot ::/boot/limine

img="$PWD/$2"

cd "$3"
#find . -type d -exec mmd -i "$img@@1M" ::{} \;
# FIXME ^^^ breaks, but there's no dirs yet, so it doesn't matter :P
find . -type f -exec mcopy -i "$img@@1M" {} ::{} \;
