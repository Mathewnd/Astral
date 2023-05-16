#include <kernel/syscalls.h>
#include <kernel/scheduler.h>
#include <errno.h>
#include <kernel/file.h>
#include <kernel/interrupt.h>

#include <logging.h>
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

	bool intstate = interrupt_set(false);
	spinlock_acquire(&proc->lock);

	nproc->parent = proc;
	nproc->sibling = proc->child;
	proc->child = nproc;

	spinlock_release(&proc->lock);
	interrupt_set(intstate);

	nproc->umask = _cpu()->thread->proc->umask;
	nproc->root = sched_getroot();
	nproc->cwd = sched_getcwd();

	ARCH_CONTEXT_THREADSAVE(nthread, ctx);

	CTX_RET(&nthread->context) = 0;
	CTX_ERRNO(&nthread->context) = 0;

	ret.ret = nproc->pid;

	sched_queue(nthread);

	cleanup:
	return ret;
}
