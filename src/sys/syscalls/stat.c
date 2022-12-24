#include <kernel/syscalls.h>
#include <sys/stat.h>
#include <kernel/vmm.h>
#include <errno.h>
#include <kernel/vfs.h>
#include <kernel/sched.h>
#include <arch/cls.h>
#include <kernel/alloc.h>
#include <string.h>
#include <kernel/ustring.h>

syscallret syscall_stat(const char* path, stat* st){
	syscallret retv;	
	retv.ret = -1;
	
	if(st > USER_SPACE_END || path > USER_SPACE_END){
		retv.errno = EFAULT;
		return retv;
	}
	
	size_t len;

	retv.errno = u_strlen(path, &len);

	if(retv.errno)
		return retv;

	char kpath[len+1];
		
	retv.errno = u_strcpy(kpath, path);

	if(retv.errno)
		return retv;

	vnode_t* file;
	
	proc_t* proc = arch_getcls()->thread->proc;

	retv.errno = vfs_open(&file, *kpath == '/' ? proc->root : proc->cwd, kpath);

	if(retv.errno)
		return retv;

	stat stbuff = file->st;

	vfs_close(file);

	retv.errno = u_memcpy(st, &stbuff, sizeof(stat));
	
	retv.ret = retv.errno ? -1 : 0;
	
	return retv;

}
