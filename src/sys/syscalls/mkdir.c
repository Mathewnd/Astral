#include <kernel/syscalls.h>
#include <kernel/vfs.h>
#include <arch/cls.h>
#include <string.h>
#include <sys/types.h>

syscallret syscall_mkdirat(int dirfd, const char* pathname, mode_t mode){
	syscallret retv;
	retv.ret = -1;

	if(pathname > USER_SPACE_END){
		retv.errno = 0;
		return retv;
	}

	proc_t* proc = arch_getcls()->thread->proc;

	char path[strlen(pathname)+1];
	
	strcpy(path, pathname);

	dirnode_t* target;
	fd_t* targetfd;

	if(dirfd != AT_FDCWD){
		
		retv.errno = fd_access(&proc->fdtable, &targetfd, dirfd);
		if(retv.errno)
			return retv;
		
		target = targetfd->node;

	}
	else target = proc->cwd;

	retv.errno = vfs_mkdir(path[0] == '/' ? proc->root : target, path, mode & ~proc->umask);
	
	if(retv.errno == 0)
		retv.ret = 0;

	if(dirfd != AT_FDCWD)
		fd_release(targetfd);

	return retv;
	
}

syscallret syscall_mkdir(const char* pathname, mode_t mode){
	return syscall_mkdirat(AT_FDCWD, pathname, mode);
}
