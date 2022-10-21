#include <kernel/syscalls.h>
#include <kernel/vfs.h>
#include <sys/types.h>
#include <arch/cls.h>
#include <kernel/fd.h>
#include <kernel/sched.h>

syscallret syscall_fchmod(int ifd, mode_t mode){
	syscallret retv;
	retv.ret = -1;	

	proc_t* proc = arch_getcls()->thread->proc;

	fd_t* fd;

	int err = fd_access(&proc->fdtable, &fd, ifd);

	if(err){
		retv.errno = err;
		return retv;
	}

	retv.errno = vfs_chmod(fd->node, mode);

	fd_release(fd);

	retv.ret = retv.errno ? -1 : 0;
	
	return retv;

}
