#include <kernel/syscalls.h>
#include <arch/cls.h>

syscallret syscall_gettid(){
	syscallret retv;
	retv.errno = 0;
	retv.ret = arch_getcls()->thread->tid;
	return retv;
}
