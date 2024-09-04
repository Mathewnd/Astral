#!/usr/bin/bash

if [ $# != 2 ]
then
	echo "usage: $0 sysrootdir initrdfullpath"
	exit 1
fi

if [ -z $1 ]
then
	echo "please pass a non empty sysroot"
	exit 1
fi

if [ -z $2 ]
then
	echo "please pass a non empty initrd full path"
	exit 1
fi

if [ -z "$(which fakeroot)" ]
then
	echo "script requires fakeroot to run, please install it."
	exit 1
fi

fakeroot << EOF
# chown to proper uids, keeping setuid bits
find $1/ ! -type l -perm -04000 -exec chown -h 0:0 {} + \
                                      -exec chmod u+s {} +
find $1/ ! -type l ! -perm -04000 -exec chown 0:0 -h {} +

find $1/home/astral ! -type l -perm -04000 -exec chown -h 1000:1000 {} + \
                                      -exec chmod u+s {} +
find $1/home/astral ! -type l ! -perm -04000 -exec chown -h 1000:1000 {} +

# create initrd
cd $1
tar --format=ustar -cf $2 *
EOF
