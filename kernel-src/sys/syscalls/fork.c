#include <kernel/syscalls.h>
#include <kernel/scheduler.h>
#include <errno.h>
#include <kernel/file.h>
#include <kernel/interrupt.h>
#include <kernel/jobctl.h>
#include <kernel/interrupt.h>
#include <arch/cpu.h>
#include <logging.h>

syscallret_t syscall_fork(context_t *ctx) {
	syscallret_t ret = {
		.ret = -1,
		.errno = 0
	};

	proc_t *proc = current_thread()->proc;
	proc_t *nproc = proc_create();
	if (nproc == NULL) {
		ret.errno = ENOMEM;
		goto cleanup;
	}

	thread_t *nthread = sched_newthread((void *)CTX_IP(ctx), 16 * PAGE_SIZE, 1, nproc, (void *)CTX_SP(ctx));
	if (nthread == NULL) {
		ret.errno = ENOMEM;
		goto cleanup;
	}

	nthread->vmmctx = vmm_fork(current_thread()->vmmctx);

	if (nthread->vmmctx == NULL) {
		ret.errno = ENOMEM;
		goto cleanup;
	}

	ret.errno = fd_clone(nproc);
	if (ret.errno)
		goto cleanup;

#ifdef __x86_64__
	if (proc->ldt) {
		ret.errno = arch_ldt_fork(&nproc->ldt, proc->ldt);
		if (ret.errno)
			goto cleanup;
	}
#endif

	MUTEX_ACQUIRE(&proc->mutex);

	nproc->parent = proc;
	nproc->sibling = proc->child;
	proc->child = nproc;

	MUTEX_RELEASE(&proc->mutex);

	nproc->umask = current_thread()->proc->umask;
	nproc->cred = current_thread()->proc->cred;
	nproc->root = proc_get_root();
	nproc->cwd = proc_get_cwd();
	nproc->threadlist = nthread;
	if (proc->flags & PROC_FLAG_SYSTRACE && !(proc->flags & PROC_FLAG_SYSTRACE_SELF))
		nproc->flags |= PROC_FLAG_SYSTRACE;

	nthread->procnext = NULL;

	ARCH_CONTEXT_THREADSAVE(nthread, ctx);

	CTX_RET(&nthread->context) = 0;
	CTX_ERRNO(&nthread->context) = 0;
	jobctl_addproc(proc, nproc);

	memcpy(&nthread->signals.mask, &current_thread()->signals.mask, sizeof(sigset_t));
	memcpy(&nthread->signals.pending, &current_thread()->signals.pending, sizeof(sigset_t));
	memcpy(&nthread->signals.stack, &current_thread()->signals.stack, sizeof(stack_t));
	memcpy(&nproc->signals.pending, &proc->signals.pending, sizeof(sigset_t));
	memcpy(&nproc->signals.actions, &proc->signals.actions[0], sizeof(sigaction_t) * NSIG);

	strncpy(nproc->name, proc->name, sizeof(proc->name));
	nproc->start_time = proc->start_time;

	// copy thread scheduling metrics over
	// raised to IPL_DPC to prevent scheduling
	long ipl = interrupt_raiseipl(IPL_DPC);

	spinlock_acquire(&proc->runtime_lock);
	nproc->total_runtime = proc->total_runtime;
	spinlock_release(&proc->runtime_lock);

	memcpy(&nthread->metrics, &current_thread()->metrics, sizeof(nthread->metrics));

	interrupt_loweripl(ipl);

	ret.ret = nproc->pid;

	sched_queue(nthread);
	// proc starts with 1 refcount, release it here as to only have the thread reference
	PROC_RELEASE(nproc);

	cleanup:
	// XXX no cleanup, TODO
	return ret;
}
