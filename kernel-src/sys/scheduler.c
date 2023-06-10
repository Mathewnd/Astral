#include <kernel/scheduler.h>
#include <kernel/slab.h>
#include <logging.h>
#include <arch/cpu.h>
#include <spinlock.h>
#include <kernel/vmm.h>
#include <kernel/alloc.h>
#include <errno.h>
#include <kernel/elf.h>
#include <kernel/file.h>
#include <semaphore.h>

#define QUANTUM_US 100000
#define SCHEDULER_STACK_SIZE 4096

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

proc_t *sched_initproc;
static pid_t currpid = 1;

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
	SPINLOCK_INIT(proc->nodeslock);
	proc->fdcount = 3;
	proc->fdfirst = 3;
	SPINLOCK_INIT(proc->fdlock);
	SEMAPHORE_INIT(&proc->waitsem, 0);

	proc->fd = alloc(sizeof(fd_t) * 3);
	if (proc->fd == NULL) {
		free(proc->threads);
		slab_free(processcache, proc);
		return NULL;
	}

	proc->pid = __atomic_fetch_add(&currpid, 1, __ATOMIC_SEQ_CST);

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
	if (proc)
		thread->tid = __atomic_fetch_add(&currpid, 1, __ATOMIC_SEQ_CST);


	CTX_INIT(&thread->context, proc != NULL);
	CTX_SP(&thread->context) = proc ? (ctxreg_t)ustack : (ctxreg_t)thread->kernelstacktop;
	CTX_IP(&thread->context) = (ctxreg_t)ip;
	SPINLOCK_INIT(thread->sleeplock);

	return thread;
}

void sched_destroythread(thread_t *thread) {
	vmm_unmap(thread->kernelstack, thread->kernelstacksize, 0);
	slab_free(threadcache, thread);
}

void sched_destroyproc(proc_t *proc) {
	//free(proc->threads);
	//free(proc->fd);
	//slab_free(processcache, proc);
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

	if (thread)
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
	thread->cpu = _cpu();
	if (current)
		current->flags &= ~SCHED_THREAD_FLAGS_RUNNING;
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

__attribute__((noreturn)) void sched_threadexit() {
	interrupt_set(false);
	thread_t *thread = _cpu()->thread;
	proc_t *proc = thread->proc;

	vmmcontext_t *oldctx = thread->vmmctx;
	vmm_switchcontext(&vmm_kernelctx);

	if (proc) {
		__atomic_fetch_sub(&proc->runningthreadcount, 1, __ATOMIC_SEQ_CST);
		if (proc->runningthreadcount == 0)
			vmm_destroycontext(oldctx);
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

	bool sleeping = thread->flags & SCHED_THREAD_FLAGS_SLEEP;

	thread_t *next = runqueuenext(sleeping ? 0x0fffffff : thread->priority);

	if (next || sleeping) {
		ARCH_CONTEXT_THREADSAVE(thread, context);
		if (sleeping)
			spinlock_release(&thread->sleeplock);
		else
			runqueueinsert(thread);

		if (next == NULL)
			next = _cpu()->idlethread;

		spinlock_release(&runqueuelock);
		switchthread(next);
	}

	spinlock_release(&runqueuelock);
}

int sched_yield() {
	bool sleeping = _cpu()->thread->flags & SCHED_THREAD_FLAGS_SLEEP;
	bool old = sleeping ? _cpu()->thread->sleepintstatus : interrupt_set(false);
	arch_context_saveandcall(yield, _cpu()->schedulerstack);
	interrupt_set(old);
	return sleeping ? _cpu()->thread->wakeupreason : 0;
}

void sched_preparesleep(bool interruptible) {
	_cpu()->thread->sleepintstatus = interrupt_set(false);
	spinlock_acquire(&_cpu()->thread->sleeplock);
	_cpu()->thread->flags |= SCHED_THREAD_FLAGS_SLEEP | (interruptible ? SCHED_THREAD_FLAGS_INTERRUPTIBLE : 0);
}

void sched_wakeup(thread_t *thread, int reason) {
	bool intstate = interrupt_set(false);
	spinlock_acquire(&thread->sleeplock);

	if ((thread->flags & SCHED_THREAD_FLAGS_SLEEP) == 0 || (reason == EINTR && (thread->flags & SCHED_THREAD_FLAGS_INTERRUPTIBLE) == 0)) {
		spinlock_release(&thread->sleeplock);
		interrupt_set(intstate);
		return;
	}

	thread->flags &= ~(SCHED_THREAD_FLAGS_SLEEP | SCHED_THREAD_FLAGS_INTERRUPTIBLE);
	thread->wakeupreason = reason;

	spinlock_release(&thread->sleeplock);
	sched_queue(thread);
	interrupt_set(intstate);
}

static vnode_t *getnodeslock(vnode_t **addr) {
	proc_t *proc = _cpu()->thread->proc;
	bool intstatus = interrupt_set(false);
	spinlock_acquire(&proc->nodeslock);
	vnode_t *vnode = *addr;
	VOP_HOLD(vnode);
	spinlock_release(&proc->nodeslock);
	interrupt_set(intstatus);
	return vnode;
}

vnode_t *sched_getcwd() {
	proc_t *proc = _cpu()->thread->proc;
	return getnodeslock(&proc->cwd);
}

vnode_t *sched_getroot() {
	proc_t *proc = _cpu()->thread->proc;
	return getnodeslock(&proc->root);
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
		_cpu()->thread = next;
		next->flags |= SCHED_THREAD_FLAGS_RUNNING;
		next->cpu = _cpu();
		if (next->vmmctx != current->vmmctx)
			vmm_switchcontext(next->vmmctx);
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

	_cpu()->schedulerstack = alloc(SCHEDULER_STACK_SIZE);
	__assert(_cpu()->schedulerstack);
	_cpu()->schedulerstack = (void *)((uintptr_t)_cpu()->schedulerstack + SCHEDULER_STACK_SIZE);

	_cpu()->schedtimerentry.func = timerhook;
	_cpu()->schedtimerentry.repeatus = QUANTUM_US;
	SPINLOCK_INIT(runqueuelock);

	_cpu()->idlethread = sched_newthread(cpuidlethread, PAGE_SIZE * 4, 3, NULL, NULL);
	__assert(_cpu()->idlethread);
	_cpu()->thread = sched_newthread(NULL, PAGE_SIZE * 32, 0, NULL, NULL);
	__assert(_cpu()->thread);

	timer_insert(_cpu()->timer, &_cpu()->schedtimerentry, QUANTUM_US);
	// XXX move this resume to a more appropriate place
	timer_resume(_cpu()->timer);
}

#define STACK_TOP (void *)0x0000800000000000
#define INTERP_BASE (void *)0x00000beef0000000

void sched_runinit() {
	printf("sched: loading /init\n");

	vmmcontext_t *vmmctx = vmm_newcontext();
	__assert(vmmctx);

	// leave kernel context to load elf
	vmm_switchcontext(vmmctx);

	proc_t *proc = sched_newproc();
	__assert(proc);

	sched_initproc = proc;

	vnode_t *initnode;
	__assert(vfs_open(vfsroot, "/usr/bin/bash", 0, &initnode) == 0);

	auxv64list_t auxv64;
	char *interp = NULL;
	void *entry;

	__assert(elf_load(initnode, NULL, &entry, &interp, &auxv64) == 0);
	if (interp) {
		vnode_t *interpnode;
		__assert(vfs_open(vfsroot, interp, 0, &interpnode) == 0);
		auxv64list_t interpauxv;
		char *interpinterp = NULL;
		__assert(elf_load(interpnode, INTERP_BASE, &entry, &interpinterp, &interpauxv) == 0);
		__assert(interpinterp == NULL);
	}

	vnode_t *stdinnode;
	__assert(vfs_open(vfsroot, "/dev/console", V_FFLAGS_READ, &stdinnode) == 0);
	vnode_t *stdoutnode;
	__assert(vfs_open(vfsroot, "/dev/console", V_FFLAGS_WRITE, &stdoutnode) == 0);
	vnode_t *stderrnode;
	__assert(vfs_open(vfsroot, "/dev/console", V_FFLAGS_WRITE, &stderrnode) == 0);

	file_t *stdin = fd_allocate();
	file_t *stdout = fd_allocate();
	file_t *stderr = fd_allocate();

	stdin->vnode = stdinnode;
	stdout->vnode = stdoutnode;
	stderr->vnode = stderrnode;

	stdin->flags = FILE_READ;
	stdout->flags = stderr->flags = FILE_WRITE;
	stdin->offset = stdout->offset = stderr->offset = 0;
	stdin->mode = stdout->mode = stderr-> mode = 0644;

	proc->fd[0].file = stdin;
	proc->fd[0].flags = 0;
	proc->fd[1].file = stdout;
	proc->fd[1].flags = 0;
	proc->fd[2].file = stderr;
	proc->fd[2].flags = 0;

	proc->cwd = vfsroot;
	VOP_HOLD(vfsroot);
	proc->root = vfsroot;
	VOP_HOLD(vfsroot);

	char *argv[] = {"/usr/bin/bash", "-l", NULL};
	char *envp[] = {NULL};

	void *stack = elf_preparestack(STACK_TOP, &auxv64, argv, envp);
	__assert(stack);

	// reenter kernel context
	vmm_switchcontext(&vmm_kernelctx);

	thread_t *uthread = sched_newthread(entry, PAGE_SIZE * 16, 1, proc, stack);
	__assert(uthread);
	uthread->vmmctx = vmmctx;
	sched_queue(uthread);

	VOP_RELEASE(initnode);
}
