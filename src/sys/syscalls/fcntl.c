#include <kernel/syscalls.h>
#include <kernel/fd.h>
#include <arch/cls.h>

#define F_DUPFD  0
#define F_GETFD  1
#define F_SETFD  2
#define F_GETFL  3
#define F_SETFL  4

#define UNKNOWN "astral: unknown fcntl\n"

syscallret syscall_fcntl(int ifd, int cmd, uint64_t arg){
	
	syscallret retv;
	retv.ret = -1;
	
	fd_t* fd = NULL;

	if(cmd != F_DUPFD){
		retv.errno = fd_access(&arch_getcls()->thread->proc->fdtable, &fd, ifd);
		if(retv.errno)
			return retv;
	}


	switch(cmd){
		case F_GETFD: // no fd flags implemented
		case F_SETFD:
			retv.ret = 0;
			break;
		case F_GETFL:
			retv.ret = fd->flags;
			break;
		case F_SETFL: // no useful status flags implemented
			retv.ret = 0;
			break;
		default:
			console_write(UNKNOWN, strlen(UNKNOWN));
			retv.errno = EINVAL;
			return retv;
	}

	if(fd)
		fd_release(fd);

	retv.errno = 0;
	return retv;

}
