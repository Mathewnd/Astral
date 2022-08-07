#ifndef _SYSMACROS_H_INCLUDE
#define _SYSMACROS_H_INCLUDE

#define makedev(major,minor) (((major & 0xFFF) << 8) + (minor & 0xFF))
#define major(dev) ((dev >> 8) & 0xFFF)
#define minor(dev) (dev & 0xFF)

#endif
