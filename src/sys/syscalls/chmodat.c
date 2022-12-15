#include <kernel/syscalls.h>
#include <kernel/vfs.h>
#include <sys/types.h>
#include <arch/cls.h>
#include <kernel/fd.h>
#include <kernel/sched.h>
#include <kernel/ustring.h>

// flags is ignored for now

syscallret syscall_fchmodat(int dirfd, const char* path, mode_t mode, int flags){
	syscallret retv;
	retv.ret = -1;
	
	
	if(path > USER_SPACE_END){
		retv.errno = EFAULT;
		return retv;
	}
	
	size_t len;

	retv.errno = u_strlen(path, &len);

	if(retv.errno)
		return retv;

	char buff[len+1];

	retv.errno = u_strcpy(buff, path);

	if(retv.errno)
		return retv;

	proc_t* proc = arch_getcls()->thread->proc;

	dirnode_t* target;
	fd_t* fd;

	if(dirfd != AT_FDCWD){
		
		int err = fd_access(&proc->fdtable, &fd, dirfd);

		if(err){
			retv.errno = err;
			return retv;
		}
		
		if(GETTYPE(fd->mode) != TYPE_DIR){
			retv.errno = ENOTDIR;
			goto _fail;
		}

		target = fd->node;
	}
	else
		target = proc->cwd;


	vnode_t* node;
	
	retv.errno = vfs_open(&node, buff[0] == '/' ? proc->root : target, buff);

	if(retv.errno)
		goto _fail;
	
	retv.errno = vfs_chmod(node, mode);

	vfs_close(node);

	_fail:

	if(dirfd != AT_FDCWD)
		fd_release(fd);

	retv.ret = retv.errno ? -1 : 0;
	
	return retv;

}
