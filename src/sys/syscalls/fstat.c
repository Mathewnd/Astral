#include <kernel/syscalls.h>
#include <sys/stat.h>
#include <kernel/fd.h>
#include <arch/cls.h>
#include <kernel/vfs.h>
#include <arch/spinlock.h>
#include <string.h>

syscallret syscall_fstat(int ifd, stat* st){
	syscallret retv;
	retv.ret = -1;

	if(st > USER_SPACE_END){
		retv.errno = EFAULT;
		return retv;
	}

	proc_t* proc = arch_getcls()->thread->proc;

        fd_t* fd;
	
	int err = fd_access(&proc->fdtable, &fd, ifd);

	if(err){
		retv.errno = err;
		return retv;
	}

	memcpy(st, &fd->node->st, sizeof(stat)); // XXX user memcpy
	
	fd_release(fd);

	retv.errno = 0;
	retv.ret = 0;

	return retv;

}
