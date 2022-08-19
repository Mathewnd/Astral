#ifndef _SYSCALLS_H_INCLUDE
#define _SYSCALLS_H_INCLUDE

#include <stddef.h>
#include <sys/types.h>

typedef struct {
        long ret;
        long errno;
} syscallret;

syscallret syscall_libc_log(char* str);
syscallret syscall_mmap(void* hint, size_t len, int prot, int flags, int fd, off_t offset);
syscallret syscall_gettid();

#endif
