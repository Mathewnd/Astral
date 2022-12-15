#include <kernel/syscalls.h>
#include <sys/stat.h>
#include <kernel/fd.h>
#include <arch/cls.h>
#include <kernel/vfs.h>
#include <arch/spinlock.h>
#include <string.h>
#include <kernel/ustring.h>

// we don't care about flags yet

syscallret syscall_fstatat(int ifd, char* path, stat* st, int flags){
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
	
	char buff[len+1];

	retv.errno = u_strcpy(buff, path);

	if(retv.errno)
		return retv;

	proc_t* proc = arch_getcls()->thread->proc;
	
	dirnode_t* targnode;
	fd_t* fd;

	if(ifd != AT_FDCWD){

		
		int err = fd_access(&proc->fdtable, &fd, ifd);

		if(err){
			retv.errno = err;
			return retv;
		}

		if(buff[0] != '/' && GETTYPE(fd->mode) != TYPE_DIR){
			retv.errno = ENOTDIR;
			goto _fail;
		}
		
		targnode = fd->node;

	}
	else
		targnode = proc->cwd;
	

	vnode_t* node;

	retv.errno = vfs_open(&node, buff[0] == '/' ? proc->root : targnode, buff);

	if(retv.errno)
		goto _fail;

	vfs_close(node);

	retv.errno = u_memcpy(st, &node->st, sizeof(stat));

	retv.ret = retv.errno ? -1 : 0;

	_fail:
	
	if(ifd != AT_FDCWD)
		fd_release(fd);

	return retv;

}
