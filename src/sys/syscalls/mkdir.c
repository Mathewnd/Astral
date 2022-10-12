#include <kernel/syscalls.h>
#include <kernel/vfs.h>
#include <arch/cls.h>
#include <string.h>
#include <sys/types.h>

syscallret syscall_mkdir(const char* pathname, mode_t mode){
	syscallret retv;
	retv.ret = -1;

	if(pathname > USER_SPACE_END){
		retv.errno = 0;
		return retv;
	}

	proc_t* proc = arch_getcls()->thread->proc;

	char path[strlen(pathname)+1];
	
	strcpy(path, pathname);

	retv.errno = vfs_mkdir(path[0] == '/' ? proc->root : proc->cwd, path, mode & ~proc->umask);
	
	if(retv.errno == 0)
		retv.ret = 0;
	

	return retv;
	
}
