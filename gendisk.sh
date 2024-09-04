#!/usr/bin/bash

if [ $# != 3 ]
then
	echo "usage: $0 size sysrootdir diskname"
	exit 1
fi

if [ -z $2 ]
then
	echo "please pass a non empty sysroot"
	exit 1
fi

if [ -z $3 ]
then
	echo "please pass a non empty disk name"
	exit 1
fi

if [ -z "$(which fakeroot)" ]
then
	echo "script requires fakeroot to run, please install it."
	exit 1
fi


PATH="/usr/sbin/:$PATH" fakeroot << EOF
# create image
truncate $3 -s $1

# chown to proper uids, keeping setuid bits
find $2/ ! -type l -perm -04000 -exec chown -h 0:0 {} + \
                                      -exec chmod u+s {} +
find $2/ ! -type l ! -perm -04000 -exec chown 0:0 -h {} +

find $2/home/astral ! -type l -perm -04000 -exec chown -h 1000:1000 {} + \
                                      -exec chmod u+s {} +
find $2/home/astral ! -type l ! -perm -04000 -exec chown -h 1000:1000 {} +

# create filesystem and copy files into image
mkfs.ext2 -O ^dir_index $3 -d $2
EOF
