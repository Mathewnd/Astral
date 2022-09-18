#include <kernel/syscalls.h>
#include <kernel/fd.h>
#include <arch/cls.h>
#include <kernel/vfs.h>
#include <arch/spinlock.h>

syscallret syscall_close(int ifd){
        syscallret retv;
        retv.ret = 0;
	retv.errno = 0;

        proc_t* proc = arch_getcls()->thread->proc;

	int err = fd_free(&proc->fdtable, ifd);

	if(err){
		retv.errno = err;
		retv.ret = -1;
	}

	return retv;
}
