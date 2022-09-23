#include <kernel/syscalls.h>
#include <kernel/fd.h>
#include <arch/cls.h>

syscallret syscall_dup(int oldfd){
	syscallret retv;
	retv.errno = fd_duplicate(&arch_getcls()->thread->proc->fdtable, oldfd, ~0, 1, &retv.ret);
	return retv;
}

syscallret syscall_dup2(int oldfd, int newfd){
	syscallret retv;
	retv.errno = fd_duplicate(&arch_getcls()->thread->proc->fdtable, oldfd, newfd, 2, &retv.ret);
	return retv;
}
