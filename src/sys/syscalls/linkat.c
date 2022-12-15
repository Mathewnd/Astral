#include <kernel/syscalls.h>
#include <kernel/vfs.h>
#include <sys/types.h>
#include <arch/cls.h>
#include <kernel/fd.h>
#include <kernel/sched.h>
#include <string.h>
#include <kernel/ustring.h>


static int gettarget(fd_t** targ, dirnode_t** targnode, int ifd, proc_t* proc){
	
	if(ifd != AT_FDCWD){
		
		int err = fd_access(&proc->fdtable, targ, ifd);

		if(err)
			return err;
		
		
		if(GETTYPE((*targ)->mode) != TYPE_DIR)
			return ENOTDIR;
		

		*targnode = (*targ)->node;
	}
	else
		*targnode = proc->cwd;

	return 0;

}


// flags is ignored for now

syscallret syscall_linkat(int srcdirfd, const char* srcpath, int tgtdirfd, const char* tgtpath, int flags){
	
	syscallret retv;
	retv.ret = -1;
	
	
	if(srcpath > USER_SPACE_END || tgtpath > USER_SPACE_END){
		retv.errno = EFAULT;
		return retv;
	}
	
	size_t srclen;
	size_t tgtlen;

	retv.errno = u_strlen(srcpath, &srclen);
	if(retv.errno)
		return retv;
	
	retv.errno = u_strlen(tgtpath, &tgtlen);
	if(retv.errno)
		return retv;
	
	char srcbuff[srclen+1];
	char tgtbuff[tgtlen+1];



	retv.errno = u_strcpy(srcbuff, srcpath);
	if(retv.errno)
		return retv;
	
	retv.errno = u_strcpy(tgtbuff, tgtpath);
	if(retv.errno)
		return retv;

	proc_t* proc = arch_getcls()->thread->proc;


	dirnode_t* source;
	fd_t* srcfd = NULL;
	dirnode_t* target;
	fd_t* tgtfd = NULL;
	
	retv.errno = gettarget(&srcfd, &source, srcdirfd, proc);

	if(retv.errno)
		goto _fail;

	retv.errno = gettarget(&tgtfd, &target, tgtdirfd, proc);

	if(retv.errno)
		goto _fail;

	vnode_t* node;
	
	retv.errno = vfs_open(&node, srcbuff[0] == '/' ? proc->root : source, srcbuff);

	if(retv.errno)
		goto _fail;

	retv.errno = vfs_link(tgtbuff[0] == '/' ? proc->root : target, node, tgtbuff);

	vfs_close(node);

	_fail:

	if(srcdirfd != AT_FDCWD && srcfd)
		fd_release(srcfd);

	if(tgtdirfd != AT_FDCWD && tgtfd)
		fd_release(tgtfd);

	retv.ret = retv.errno ? -1 : 0;
	return retv;

}
