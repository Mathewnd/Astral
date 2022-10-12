#include <kernel/syscalls.h>
#include <arch/cls.h>


syscallret syscall_umask(mode_t mask){
	
	syscallret retv;
	retv.errno = 0;
	
	proc_t* proc = arch_getcls()->thread->proc;
	
	retv.ret = proc->umask;
	proc->umask = mask & 0777;
	
	return retv;
	
}
