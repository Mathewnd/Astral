#include <kernel/scheduler.h>
#include <kernel/slab.h>
#include <logging.h>
#include <arch/cpu.h>
#include <spinlock.h>
#include <kernel/vmm.h>
#include <kernel/alloc.h>

#define QUANTUM_US 100000

static scache_t *threadcache;
static scache_t *processcache;

#define RUNQUEUE_COUNT 64

typedef struct {
	thread_t *list;
	thread_t *last;
} rqueue_t;

static rqueue_t runqueue[RUNQUEUE_COUNT];
static uint64_t runqueuebitmap;
static spinlock_t runqueuelock;

proc_t *sched_newproc() {
	proc_t *proc = slab_allocate(processcache);
	if (proc == NULL)
		return NULL;

	proc->threads = alloc(sizeof(thread_t *));
	if (proc->threads == NULL) {
		slab_free(processcache, proc);
		return NULL;
	}
	// TODO move this to slab
	proc->threadtablesize = 1;
	proc->runningthreadcount = 1;
	SPINLOCK_INIT(proc->lock);

	return proc;
}

thread_t *sched_newthread(void *ip, size_t kstacksize, int priority, proc_t *proc, void *ustack) {
	thread_t *thread = slab_allocate(threadcache);
	if (thread == NULL)
		return NULL;

	thread->kernelstack = vmm_map(NULL, kstacksize, VMM_FLAGS_ALLOCATE, ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_NOEXEC, NULL);
	if (thread->kernelstack == NULL) {
		slab_free(threadcache, thread);
		return NULL;
	}

	thread->kernelstacktop = (void *)((uintptr_t)thread->kernelstack + kstacksize);

	// non kernel thread vmm contexts are handled by the caller
	thread->vmmctx = proc ? NULL : &vmm_kernelctx;
	thread->proc = proc;
	thread->priority = priority;

	CTX_INIT(&thread->context, proc != NULL);
	CTX_SP(&thread->context) = proc ? (ctxreg_t)ustack : (ctxreg_t)thread->kernelstacktop;
	CTX_IP(&thread->context) = (ctxreg_t)ip;

	return thread;
}

void sched_destroythread(thread_t *thread) {
	vmm_unmap(thread->kernelstack, thread->kernelstacksize, 0);
	slab_free(threadcache, thread);
}

void sched_destroyproc(proc_t *proc) {
	free(proc->threads);
	slab_free(processcache, proc);
}

static thread_t *runqueuenext(int minprio) {
	bool intstate = interrupt_set(false);

	thread_t *thread = NULL;

	if (runqueuebitmap == 0)
		goto leave;

	// TODO use the bitmap for this
	for (int i = 0; i < RUNQUEUE_COUNT && i <= minprio; ++i) {
		if (runqueue[i].list) {
			thread = runqueue[i].list;
			runqueue[i].list = thread->next;
			if (runqueue[i].list == NULL) {
				runqueue[i].last = NULL;
				runqueuebitmap &= ~((uint64_t)1 << i);
			}
			break;
		}
	}

	thread->flags &= ~SCHED_THREAD_FLAGS_QUEUED;

	leave:
	interrupt_set(intstate);
	return thread;
}

static __attribute__((noreturn)) void switchthread(thread_t *thread) {
	interrupt_set(false);
	thread_t* current = _cpu()->thread;
	
	_cpu()->thread = thread;

	if(current == NULL || thread->vmmctx != current->vmmctx)
		vmm_switchcontext(thread->vmmctx);

	_cpu()->intstatus = ARCH_CONTEXT_INTSTATUS(&thread->context);
	ARCH_CONTEXT_SWITCHTHREAD(thread);

	__builtin_unreachable();
}

static void runqueueinsert(thread_t *thread) {
	thread->flags |= SCHED_THREAD_FLAGS_QUEUED;

	runqueuebitmap |= ((uint64_t)1 << thread->priority);

	thread->prev = runqueue[thread->priority].last;
	if (thread->prev)
		thread->prev->next = thread;
	else
		runqueue[thread->priority].list = thread;

	thread->next = NULL;
	runqueue[thread->priority].last = thread;
}

void sched_queue(thread_t *thread) {
	bool intstate = interrupt_set(false);
	spinlock_acquire(&runqueuelock);

	// maybe instead of an assert, a simple return would suffice as the thread would already be queued anyways
	__assert((thread->flags & SCHED_THREAD_FLAGS_QUEUED) == 0 && (thread->flags & SCHED_THREAD_FLAGS_RUNNING) == 0);

	runqueueinsert(thread);

	spinlock_release(&runqueuelock);
	interrupt_set(intstate);
	// TODO yield if higher priority than current thread (or send another CPU an IPI)
}

__attribute__((noreturn)) void sched_stopcurrentthread() {
	interrupt_set(false);
	if (_cpu()->thread)
		_cpu()->thread->flags &= ~SCHED_THREAD_FLAGS_RUNNING;

	spinlock_acquire(&runqueuelock);

	thread_t *next = runqueuenext(0x0fffffff);
	if (next == NULL)
		next = _cpu()->idlethread;

	spinlock_release(&runqueuelock);

	switchthread(next);
}

void sched_threadexit() {
	interrupt_set(false);
	thread_t *thread = _cpu()->thread;
	proc_t *proc = thread->proc;

	if (proc) {
		spinlock_acquire(&proc->lock);

		int toffset = -1;

		for (int i = 0; i < proc->threadtablesize; ++i) {
			if (proc->threads[i] == thread) {
				toffset = i;
				break;
			}
		}

		__assert(toffset != -1);
		--proc->runningthreadcount;

		if (proc->runningthreadcount == 0) {
			// TODO prepare proc state to be left as a zombie
		}
		spinlock_release(&proc->lock);
	}

	_cpu()->thread = NULL;

	// because a thread deallocating its own data is a nightmare, thread deallocation and such will be left to whoever frees the proc it's tied to
	// (likely an exit(2) call)
	// FIXME this doesn't apply to kernel threads and something should be figured out for them

	sched_stopcurrentthread();
}

static void yield(context_t *context) {
	thread_t *thread = _cpu()->thread;

	spinlock_acquire(&runqueuelock);

	thread_t *next = runqueuenext(thread->priority);

	if (next) {
		ARCH_CONTEXT_THREADSAVE(thread, context);
		runqueueinsert(thread);

		spinlock_release(&runqueuelock);
		switchthread(next);
	}

	spinlock_release(&runqueuelock);
}

void sched_yield() {
	bool old = interrupt_set(false);
	arch_context_saveandcall(yield);
	interrupt_set(old);
}

static void timerhook(void *private, context_t *context) {
	thread_t* current = _cpu()->thread;

	spinlock_acquire(&runqueuelock);

	thread_t *next = runqueuenext(current->priority);

	if (next) {
		current->flags &= ~SCHED_THREAD_FLAGS_RUNNING;
		ARCH_CONTEXT_THREADSAVE(current, context);

		runqueueinsert(current);

		ARCH_CONTEXT_THREADLOAD(next, context);
		if (next->vmmctx != current->vmmctx)
			vmm_switchcontext(next->vmmctx);
		next->flags |= SCHED_THREAD_FLAGS_RUNNING;
		_cpu()->thread = next;
	}

	spinlock_release(&runqueuelock);
}

static void cpuidlethread() {
	// TODO check every interrupt rather than waiting for a scheduler timer interrupt
	interrupt_set(true);
	while (1)
		CPU_HALT();
}

void sched_init() {
	threadcache = slab_newcache(sizeof(thread_t), 0, NULL, NULL);
	__assert(threadcache);
	processcache = slab_newcache(sizeof(proc_t), 0, NULL, NULL);
	__assert(processcache);

	_cpu()->schedtimerentry.func = timerhook;
	_cpu()->schedtimerentry.repeatus = QUANTUM_US;
	SPINLOCK_INIT(runqueuelock);

	_cpu()->idlethread = sched_newthread(cpuidlethread, PAGE_SIZE * 4, 0, NULL, NULL);
	__assert(_cpu()->idlethread);
	_cpu()->thread = sched_newthread(NULL, PAGE_SIZE * 32, 0, NULL, NULL);
	__assert(_cpu()->thread);

	timer_insert(_cpu()->timer, &_cpu()->schedtimerentry, QUANTUM_US);
	// XXX move this resume to a more appropriate place
	timer_resume(_cpu()->timer);
}
