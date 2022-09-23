#include <kernel/syscalls.h>
#include <arch/cls.h>

syscallret syscall_getpid(){
	syscallret retv = { arch_getcls()->thread->proc->pid, 0 };
	return retv;
}
