#include <kernel/syscalls.h>
#include <kernel/fd.h>
#include <kernel/alloc.h>
#include <kernel/vfs.h>
#include <arch/cls.h>
#include <kernel/sched.h>
#include <arch/spinlock.h>
#include <kernel/vmm.h>

syscallret syscall_read(int ifd, void* buff, size_t count){
	
	syscallret retv;
	retv.ret = -1;

	if(buff > USER_SPACE_END){
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

	void* kbuff = alloc(count);

	if(!kbuff){
		fd_release(fd);	
		retv.errno = ENOMEM;
		return retv;
	}

	size_t readc = vfs_read(&err, fd->node, kbuff, count, fd->offset);



	if(err){
		fd_release(fd);	
		free(kbuff);
		retv.errno = err;
		return retv;
	}

	fd->offset += readc;
	
	fd_release(fd);	

	memcpy(buff, kbuff, count); // XXX user version for safety

	retv.errno = 0;
	retv.ret = readc;

	free(kbuff);
	
	return retv;
}
