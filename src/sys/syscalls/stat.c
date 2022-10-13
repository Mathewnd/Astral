#include <kernel/syscalls.h>
#include <sys/stat.h>
#include <kernel/vmm.h>
#include <errno.h>
#include <kernel/vfs.h>
#include <kernel/sched.h>
#include <arch/cls.h>
#include <kernel/alloc.h>
#include <string.h>

syscallret syscall_stat(const char* path, stat* st){
	syscallret retv;	
	retv.ret = -1;
	
	if(st > USER_SPACE_END || path > USER_SPACE_END){
		retv.errno = EFAULT;
		return retv;
	}

	char* kpath = alloc(strlen(path)+1); // XXX use proper user strlen func
	
	if(!kpath){
		retv.errno = ENOMEM;
		return retv;
	}

	strcpy(kpath, path); // XXX use user strcpy func

	vnode_t* file;
	
	proc_t* proc = arch_getcls()->thread->proc;

	int ret = vfs_open(&file, *kpath == '/' ? proc->root : proc->cwd, kpath);

	if(ret){
		retv.errno = ret;
		goto _ret;
	}

	stat stbuff = file->st;

	vfs_close(file);

	memcpy(st, &stbuff, sizeof(stat)); // XXX use user memcpy
	
	retv.ret = 0;
	retv.errno = 0;
	_ret:
	free(kpath);
	return retv;

}
