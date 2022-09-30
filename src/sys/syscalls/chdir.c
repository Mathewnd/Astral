#include <kernel/syscalls.h>
#include <kernel/vmm.h>
#include <kernel/vfs.h>
#include <errno.h>
#include <string.h>
#include <arch/cls.h>
#include <kernel/sched.h>
#include <sys/stat.h>

syscallret syscall_chdir(const char* path){
	syscallret retv;

	retv.ret = -1;

	if(path > USER_SPACE_END){
		retv.errno = EFAULT;
		return retv;
	}

	size_t len = strlen(path);
	char buff[len+1];

	strcpy(buff, path);

	proc_t* proc = arch_getcls()->thread->proc;

	vnode_t* node;

	retv.errno = vfs_open(&node, buff[0] == '/' ? proc->root : proc->cwd, buff);

	if(retv.errno)
		return retv;

	if(GETTYPE(node->st.st_mode) != TYPE_DIR){
		vfs_close(node);
		retv.errno = ENOTDIR;
		return retv;
	}
	
	vnode_t* old = proc->cwd;
	proc->cwd = node;

	vfs_close(old);
	
	retv.ret = 0;

	return retv;


}
