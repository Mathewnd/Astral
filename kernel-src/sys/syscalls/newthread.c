#include <kernel/syscalls.h>
#include <kernel/scheduler.h>
#include <kernel/cmdline.h>
#include <kernel/alloc.h>

syscallret_t syscall_newthread(context_t *, void *entry, void *stack) {
	syscallret_t ret = {
		.ret = -1
	};

	// if the kernel was specified to have no user threading in the cmdline
	if (cmdline_get("nomultithread")) {
		ret.errno = ENOSYS;
		return ret;
	}

	proc_t *proc = _cpu()->thread->proc;

	spinlock_acquire(&proc->lock);

	// if we currently don't want any more threads to start
	if (proc->nomorethreads == true) {
		spinlock_release(&proc->lock);
		ret.errno = EAGAIN;
		return ret;
	}

	int i;
	for (i = 0; i < proc->threadtablesize; ++i) {
		thread_t *thread = proc->threads[i];
		if (thread == NULL || (thread->flags & SCHED_THREAD_FLAGS_DEAD) == 0)
			continue;

		// reuse the slot in the threads table
		sched_destroythread(thread);
		break;
	}

	if (i == proc->threadtablesize) {
		// resize it
		void *tmp = realloc(proc->threads, (proc->threadtablesize + 1) * sizeof(thread_t *));
		if (tmp == NULL) {
			ret.errno = ENOMEM;
			goto cleanup;
		}

		proc->threads = tmp;
	}

	proc->threads[i] = sched_newthread(entry, 16 * PAGE_SIZE, 1, _cpu()->thread->proc, stack);
	if (proc->threads[i] == NULL) {
		ret.errno = ENOMEM;
		goto cleanup;
	}

	proc->threads[i]->vmmctx = _cpu()->thread->vmmctx;

	ret.errno = 0;
	ret.ret = proc->threads[i]->tid;

	__atomic_fetch_add(&proc->runningthreadcount, 1, __ATOMIC_SEQ_CST);

	sched_queue(proc->threads[i]);

	cleanup:
	spinlock_release(&proc->lock);
	return ret;
}
