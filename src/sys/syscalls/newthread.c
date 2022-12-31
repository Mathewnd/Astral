#include <kernel/syscalls.h>
#include <kernel/sched.h>
#include <kernel/env.h>
#include <arch/cls.h>

syscallret syscall_newthread(void* nip, void* stack){
	syscallret retv;
	retv.ret = -1;

	if(env_isset("nomultithread")){
		retv.errno = ENOSYS;
		return retv;
	}
	
	thread_t* thread = arch_getcls()->thread;
	proc_t* proc = thread->proc;

	thread_t* new = sched_newuthread(nip, THREAD_DEFAULT_KSTACK_SIZE, stack, proc, false, THREAD_PRIORITY_USER);

	if(!new){
		retv.errno = ENOMEM;
		return retv;
	}	
	
	new->ctx = thread->ctx;
	
	sched_queuethread(new);

	retv.errno = 0;
	retv.ret = new->tid;

	return retv;
	
}
