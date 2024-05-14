#include <kernel/syscalls.h>
#include <kernel/scheduler.h>
#include <errno.h>
#include <kernel/file.h>
#include <kernel/interrupt.h>
#include <kernel/jobctl.h>

syscallret_t syscall_fork(context_t *ctx) {
	syscallret_t ret = {
		.ret = -1,
		.errno = 0
	};

	proc_t *proc = _cpu()->thread->proc;
	proc_t *nproc = sched_newproc();
	if (nproc == NULL) {
		ret.errno = ENOMEM;
		goto cleanup;
	}

	thread_t *nthread = sched_newthread((void *)CTX_IP(ctx), 16 * PAGE_SIZE, 1, nproc, (void *)CTX_SP(ctx));
	if (nthread == NULL) {
		ret.errno = ENOMEM;
		goto cleanup;
	}

	nthread->vmmctx = vmm_fork(_cpu()->thread->vmmctx);

	if (nthread->vmmctx == NULL) {
		ret.errno = ENOMEM;
		goto cleanup;
	}

	ret.errno = fd_clone(nproc);
	if (ret.errno)
		goto cleanup;

	MUTEX_ACQUIRE(&proc->mutex, false);

	nproc->parent = proc;
	nproc->sibling = proc->child;
	proc->child = nproc;

	MUTEX_RELEASE(&proc->mutex);

	nproc->umask = _cpu()->thread->proc->umask;
	nproc->root = sched_getroot();
	nproc->cwd = sched_getcwd();
	nproc->threads[0] = nthread;

	ARCH_CONTEXT_THREADSAVE(nthread, ctx);

	CTX_RET(&nthread->context) = 0;
	CTX_ERRNO(&nthread->context) = 0;
	jobctl_addproc(proc, nproc);

	ret.ret = nproc->pid;

	sched_queue(nthread);
	// proc starts with 1 refcount, release it here as to only have the thread reference
	PROC_RELEASE(nproc);

	cleanup:
	return ret;
}
