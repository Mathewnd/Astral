#include <kernel/syscalls.h>
#include <kernel/fd.h>
#include <kernel/vfs.h>
#include <arch/cls.h>
#include <kernel/sched.h>
#include <arch/spinlock.h>

syscallret syscall_ioctl(int ifd, unsigned long request, void* arg){
	syscallret retv;
        retv.ret = -1;
	
	proc_t* proc = arch_getcls()->thread->proc;

        fd_t* fd;
	
	int ret = fd_access(&proc->fdtable, &fd, ifd);

	if(ret){
		retv.errno = ret;
		return retv;
	}
	
	retv.ret = 0;
	retv.errno = vfs_ioctl(fd->node, request, arg, &retv.ret);

	fd_release(fd);

	return retv;
	
}
