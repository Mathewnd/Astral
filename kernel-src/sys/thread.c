#include <kernel/thread.h>
#include <kernel/proc.h>
#include <kernel/slab.h>
#include <logging.h>

static scache_t *thread_cache;

thread_t *sched_newthread(void *ip, size_t kstacksize, int priority, proc_t *proc, void *ustack) {
	if (thread_cache == NULL) {
		thread_cache = slab_newcache(sizeof(thread_t), 0, NULL, NULL);
		__assert(thread_cache);
	}

	thread_t *thread = slab_allocate(thread_cache);
	if (thread == NULL)
		return NULL;

	memset(thread, 0, sizeof(thread_t));

	thread->kernelstack = vmm_map(NULL, kstacksize, VMM_FLAGS_ALLOCATE, ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_NOEXEC, NULL);
	if (thread->kernelstack == NULL) {
		slab_free(thread_cache, thread);
		return NULL;
	}

	thread->kernelstacktop = (void *)((uintptr_t)thread->kernelstack + kstacksize);

	// non kernel thread vmm contexts are handled by the caller
	thread->vmmctx = proc ? NULL : &vmm_kernelctx;
	thread->proc = proc;
	thread->priority = priority;
	thread->kernelstacksize = kstacksize;
	if (proc) {
		// each thread holds one reference to proc
		PROC_HOLD(proc);
		thread->tid = proc_allocate_pid();
	}

	CTX_INIT(&thread->context, proc != NULL, true);
	CTX_XINIT(&thread->extracontext, proc != NULL);
	CTX_SP(&thread->context) = proc ? (ctxreg_t)ustack : (ctxreg_t)thread->kernelstacktop;
	CTX_IP(&thread->context) = (ctxreg_t)ip;
	SPINLOCK_INIT(thread->sleeplock);
	SPINLOCK_INIT(thread->signals.lock);
	EVENT_INITHEADER(&thread->signals.waitpendingevent);

	return thread;
}

void sched_destroythread(thread_t *thread) {
	vmm_unmap(thread->kernelstack, thread->kernelstacksize, 0);
	slab_free(thread_cache, thread);
}

static void threadexit_internal(context_t *, void *) {
	thread_t *thread = current_thread();
	// we don't need to access the current thread anymore, as any state will be ignored and we are running on the scheduler stack
	// as well as not being preempted at all
	interrupt_set(false);
	current_cpu()->thread = NULL;

	thread->flags |= THREAD_FLAGS_DEAD;

	// because a thread deallocating its own data is a nightmare, thread deallocation and such will be left to whoever frees the proc it's tied to
	// (likely an exit(2) call)
	// FIXME this doesn't apply to kernel threads and something should be figured out for them

	sched_stop_current_thread();
}

__attribute__((noreturn)) void sched_threadexit() {
	thread_t *thread = current_thread();
	proc_t *proc = thread->proc;

	vmmcontext_t *oldctx = thread->vmmctx;
	vmm_switchcontext(&vmm_kernelctx);

	if (proc) {
		__atomic_fetch_sub(&proc->runningthreadcount, 1, __ATOMIC_SEQ_CST);
		__assert(oldctx != &vmm_kernelctx);
		if (proc->runningthreadcount == 0) {
			if (thread->shouldexit)
				proc->status = -1;

			proc_exit();
			vmm_destroycontext(oldctx);
			PROC_RELEASE(proc);
		}
	}

	interrupt_set(false);
	arch_context_saveandcall(threadexit_internal, current_cpu()->schedulerstack, NULL);
	__builtin_unreachable();
}
