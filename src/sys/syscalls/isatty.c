#include <kernel/syscalls.h>
#include <kernel/fd.h>
#include <kernel/vfs.h>
#include <arch/cls.h>
#include <kernel/sched.h>
#include <arch/spinlock.h>

syscallret syscall_isatty(int ifd){
	
	syscallret retv;
        retv.ret = -1;
	
	proc_t* proc = arch_getcls()->thread->proc;

        fd_t* fd;
	
	int ret = fd_access(&proc->fdtable, &fd, ifd);

	if(ret){
		retv.errno = ret;
		return retv;
	}

	ret = vfs_isatty(fd->node);
	
	retv.errno = ret;

	retv.ret = retv.errno == ENOTTY ? 0 : 1;

	fd_release(fd);

	return retv;
	
}
