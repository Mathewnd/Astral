#ifndef _SYSCALLS_H_INCLUDE
#define _SYSCALLS_H_INCLUDE

typedef struct {
        long ret;
        long errno;
} syscallret;

syscallret syscall_libc_log(char* str);

#endif
