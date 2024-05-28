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
#include <kernel/devfs.h>
#include <kernel/jobctl.h>
#include <kernel/cmdline.h>

#define QUANTUM_US 100000
#define SCHEDULER_STACK_SIZE PAGE_SIZE * 16

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
static hashtable_t pidtable;
// not defined as static because its acquired/released from a macro in kernel/scheduler.h
mutex_t sched_pidtablemutex;

proc_t *sched_initproc;
static pid_t currpid = 1;

proc_t *sched_getprocfrompid(int pid) {
	MUTEX_ACQUIRE(&sched_pidtablemutex, false);
	void *_proc = NULL;
	hashtable_get(&pidtable, &_proc, &pid, sizeof(pid));
	proc_t *proc = _proc;
	if (proc) {
		PROC_HOLD(proc);
	}
	MUTEX_RELEASE(&sched_pidtablemutex);

	return proc;
}

proc_t *sched_newproc() {
	proc_t *proc = slab_allocate(processcache);
	if (proc == NULL)
		return NULL;

	memset(proc, 0, sizeof(proc_t));

	proc->threads = alloc(sizeof(thread_t *));
	if (proc->threads == NULL) {
		slab_free(processcache, proc);
		return NULL;
	}

	// TODO move this to slab
	proc->state = SCHED_PROC_STATE_NORMAL;
	proc->threadtablesize = 1;
	proc->runningthreadcount = 1;
	MUTEX_INIT(&proc->mutex);
	SPINLOCK_INIT(proc->nodeslock);
	proc->fdcount = 3;
	proc->refcount = 1;
	proc->fdfirst = 3;
	MUTEX_INIT(&proc->fdmutex);
	SEMAPHORE_INIT(&proc->waitsem, 0);
	SPINLOCK_INIT(proc->jobctllock);
	SPINLOCK_INIT(proc->pgrp.lock);
	SPINLOCK_INIT(proc->signals.lock);

	proc->fd = alloc(sizeof(fd_t) * 3);
	if (proc->fd == NULL) {
		free(proc->threads);
		slab_free(processcache, proc);
		return NULL;
	}

	proc->pid = __atomic_fetch_add(&currpid, 1, __ATOMIC_SEQ_CST);
	proc->umask = 022; // default umask

	// add to pid table
	MUTEX_ACQUIRE(&sched_pidtablemutex, false);
	if (hashtable_set(&pidtable, proc, &proc->pid, sizeof(proc->pid), true)) {
		MUTEX_RELEASE(&sched_pidtablemutex);
		free(proc->fd);
		free(proc->threads);
		slab_free(processcache, proc);
		return NULL;
	}
	MUTEX_RELEASE(&sched_pidtablemutex);

	return proc;
}

thread_t *sched_newthread(void *ip, size_t kstacksize, int priority, proc_t *proc, void *ustack) {
	thread_t *thread = slab_allocate(threadcache);
	if (thread == NULL)
		return NULL;

	memset(thread, 0, sizeof(thread_t));

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
	thread->kernelstacksize = kstacksize;
	if (proc) {
		// each thread holds one reference to proc
		PROC_HOLD(proc);
		thread->tid = __atomic_fetch_add(&currpid, 1, __ATOMIC_SEQ_CST);
	}


	CTX_INIT(&thread->context, proc != NULL, true);
	CTX_XINIT(&thread->extracontext, proc != NULL);
	CTX_SP(&thread->context) = proc ? (ctxreg_t)ustack : (ctxreg_t)thread->kernelstacktop;
	CTX_IP(&thread->context) = (ctxreg_t)ip;
	SPINLOCK_INIT(thread->sleeplock);
	SPINLOCK_INIT(thread->signals.lock);

	return thread;
}

void sched_destroythread(thread_t *thread) {
	vmm_unmap(thread->kernelstack, thread->kernelstacksize, 0);
	slab_free(threadcache, thread);
}

void sched_destroyproc(proc_t *proc) {
	free(proc->threads);
	free(proc->fd);
	slab_free(processcache, proc);
}

static thread_t *getinrunqueue(rqueue_t *rq) {
	thread_t *thread = rq->list;

	while (thread) {
		if (thread->cputarget == NULL || thread->cputarget == _cpu())
			break;

		thread = thread->next;
	}

	if (thread) {
		if (thread->prev)
			thread->prev->next = thread->next;
		else
			rq->list = thread->next;

		if (thread->next)
			thread->next->prev = thread->prev;
		else
			rq->last = thread->prev;
	}

	return thread;
}

static thread_t *runqueuenext(int minprio) {
	bool intstate = interrupt_set(false);

	thread_t *thread = NULL;

	if (runqueuebitmap == 0)
		goto leave;

	// TODO use the bitmap for this
	for (int i = 0; i < RUNQUEUE_COUNT && i <= minprio && thread == NULL; ++i) {
		thread = getinrunqueue(&runqueue[i]);
		if (thread && runqueue[i].list == NULL)
			runqueuebitmap &= ~((uint64_t)1 << i);
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
	// XXX make the locking of the flags field better, candidate for a r/w lock
	spinlock_acquire(&runqueuelock);
	if (current)
		current->flags &= ~SCHED_THREAD_FLAGS_RUNNING;

	__assert((thread->flags & SCHED_THREAD_FLAGS_RUNNING) == 0);

	if (current && current->flags & SCHED_THREAD_FLAGS_SLEEP)
		spinlock_release(&current->sleeplock);

	thread->flags |= SCHED_THREAD_FLAGS_RUNNING;
	__assert((thread->flags & SCHED_THREAD_FLAGS_QUEUED) == 0);
	spinlock_release(&runqueuelock);

	void *schedulerstack = _cpu()->schedulerstack;
	__assert(!((void *)thread->context.rsp < schedulerstack && (void *)thread->context.rsp >= (schedulerstack - SCHEDULER_STACK_SIZE)));

	ARCH_CONTEXT_SWITCHTHREAD(thread);
	__builtin_unreachable();
}

static void runqueueinsert(thread_t *thread) {
	__assert((thread->flags & SCHED_THREAD_FLAGS_RUNNING) == 0);
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

	spinlock_acquire(&runqueuelock);
	if (_cpu()->thread)
		_cpu()->thread->flags &= ~SCHED_THREAD_FLAGS_RUNNING;

	thread_t *next = runqueuenext(0x0fffffff);
	if (next == NULL)
		next = _cpu()->idlethread;

	spinlock_release(&runqueuelock);

	switchthread(next);
}

// called when all threads in a process have exited
void sched_procexit() {
	proc_t *proc = _cpu()->thread->proc;
	__assert(proc != sched_initproc);

	// the process is going to be referenced by the child list, hold it
	// it only holds it now because it has to have running threads to be in the list
	// which hold a reference. it won't now since its a zombie, which is why it will be held
	PROC_HOLD(proc);

	jobctl_detach(proc);

	// close fds
	for (int fd = 0; fd < proc->fdcount; ++fd)
		fd_close(fd);

	// zombify the proc
	MUTEX_ACQUIRE(&proc->mutex, false);

	VOP_RELEASE(proc->root);
	VOP_RELEASE(proc->cwd);

	proc->state = SCHED_PROC_STATE_ZOMBIE;
	proc_t *lastchild = proc->child;


	MUTEX_ACQUIRE(&sched_initproc->mutex, false);

	int belowzombiecount = 0;

	while (lastchild && lastchild->sibling) {
		if (lastchild->state == SCHED_PROC_STATE_ZOMBIE)
			++belowzombiecount;

		lastchild->parent = sched_initproc;
		lastchild = lastchild->sibling;
	}

	if (lastchild) {
		lastchild->sibling = sched_initproc->child;
		sched_initproc->child = proc->child;
	}

	MUTEX_RELEASE(&sched_initproc->mutex);
	MUTEX_RELEASE(&proc->mutex);

	signal_signalproc(proc->parent, SIGCHLD);
	semaphore_signal(&proc->parent->waitsem);
	// TODO sigaction flag for this
	for (int i = 0; i < belowzombiecount; ++i)
		semaphore_signal(&sched_initproc->waitsem);
}

void sched_inactiveproc(proc_t *proc) {
	// proc refcount 0, we can free the structure
	// the pid table lock is already acquired.

	hashtable_remove(&pidtable, &proc->pid, sizeof(proc->pid));

	//arch_e9_puts("\n\ndestroy proc\n\n");
	//printf("destroy proc\n");
	sched_destroyproc(proc);
}

static void threadexit_internal(context_t *, void *) {
	thread_t *thread = _cpu()->thread;
	// we don't need to access the current thread anymore, as any state will be ignored and we are running on the scheduler stack
	// as well as not being preempted at all
	_cpu()->thread = NULL;

	interrupt_set(false);
	spinlock_acquire(&runqueuelock);
	thread->flags |= SCHED_THREAD_FLAGS_DEAD;
	spinlock_release(&runqueuelock);

	// because a thread deallocating its own data is a nightmare, thread deallocation and such will be left to whoever frees the proc it's tied to
	// (likely an exit(2) call)
	// FIXME this doesn't apply to kernel threads and something should be figured out for them

	sched_stopcurrentthread();
}

__attribute__((noreturn)) void sched_threadexit() {
	thread_t *thread = _cpu()->thread;
	proc_t *proc = thread->proc;

	vmmcontext_t *oldctx = thread->vmmctx;
	vmm_switchcontext(&vmm_kernelctx);

	if (proc) {
		__atomic_fetch_sub(&proc->runningthreadcount, 1, __ATOMIC_SEQ_CST);
		__assert(oldctx != &vmm_kernelctx);
		if (proc->runningthreadcount == 0) {
			if (thread->shouldexit)
				proc->status = -1;

			sched_procexit();
			vmm_destroycontext(oldctx);
			PROC_RELEASE(proc);
		}
	}

	interrupt_set(false);
	arch_context_saveandcall(threadexit_internal, _cpu()->schedulerstack, NULL);
	__builtin_unreachable();
}

typedef struct {
	context_t *context;
	bool syscall;
	uint64_t syscallerrno;
	uint64_t syscallret;
} checkargs_t;

// SHOULD NOT BE CALLED WITH THE SCHEDULER STACK
static void userspacecheck(void *_args) {
	checkargs_t *args = _args;
	__assert(_cpu() >= (cpu_t *)0xffffffff80000000);
	thread_t *thread = _cpu()->thread;

	if (thread->shouldexit) {
		interrupt_set(true);
		sched_threadexit();
	}

	while (signal_check(thread, args->context, args->syscall, args->syscallret, args->syscallerrno)) ;
}

static void checktrampoline(context_t *context, void *_args) {
	__assert(ARCH_CONTEXT_INTSTATUS(context) == false);
	checkargs_t *args = _args;
	userspacecheck(args);
	_cpu()->intstatus = ARCH_CONTEXT_INTSTATUS(args->context);
	arch_context_switch(args->context);
}

// called right before going back to userspace in the syscall handler, interrupt handler and arch_context_switch
__attribute__((no_caller_saved_registers)) void sched_userspacecheck(context_t *context, bool syscall, uint64_t syscallerrno, uint64_t syscallret) {
	__assert(_cpu());
	if (_cpu()->thread == NULL || ARCH_CONTEXT_ISUSER(context) == false)
		return;

	bool intstatus = interrupt_set(false);

	checkargs_t args = {
		.context = context,
		.syscall = syscall,
		.syscallerrno = syscallerrno,
		.syscallret = syscallret
	};

	// return context is user mode, so its kernel stack is free
	// if we are running on the scheduler stack, run on the kernel stack
	// so we don't risk stack corruption (as we have to turn on interrupts should the thread need to exit)
	if (&args < (checkargs_t *)_cpu()->schedulerstack && &args >= (checkargs_t *)((uintptr_t)_cpu()->schedulerstack - SCHEDULER_STACK_SIZE)) {
		arch_context_saveandcall(checktrampoline, _cpu()->thread->kernelstacktop, &args);
	} else {
		userspacecheck(&args);
	}

	_cpu()->intstatus = intstatus;
}

void sched_stopotherthreads() {
	thread_t *thread = _cpu()->thread;
	proc_t *proc = thread->proc;
	MUTEX_ACQUIRE(&proc->mutex, false);
	proc->nomorethreads = true;

	for (int i = 0; i < proc->threadtablesize; ++i) {
		if (proc->threads[i] == NULL || proc->threads[i] == thread)
			continue;

		proc->threads[i]->shouldexit = true;
		sched_wakeup(proc->threads[i], SCHED_WAKEUP_REASON_INTERRUPTED);
		proc->threads[i] = NULL;
	}

	MUTEX_RELEASE(&proc->mutex);

	while (__atomic_load_n(&proc->runningthreadcount, __ATOMIC_SEQ_CST) > 1) sched_yield();

	proc->nomorethreads = false;
}

void sched_terminateprogram(int status) {
	thread_t *thread = _cpu()->thread;
	proc_t *proc = thread->proc;
	if (spinlock_try(&proc->exiting) == false)
		sched_threadexit();

	sched_stopotherthreads();

	proc->status = status;

	sched_threadexit();
}

static void yield(context_t *context, void *) {
	thread_t *thread = _cpu()->thread;

	spinlock_acquire(&runqueuelock);

	bool sleeping = thread->flags & SCHED_THREAD_FLAGS_SLEEP;

	thread_t *next = runqueuenext(sleeping ? 0x0fffffff : thread->priority);
	bool gotsignal = false;
	for (int i = 0; i < NSIG && thread->proc; ++i) {
		void *action = thread->proc->signals.actions[i].address;
		if (SIGNAL_GET(&thread->signals.urgent, i)) {
			gotsignal = true;
			break;
		}
		if (action == SIG_IGN || (action == SIG_DFL && signal_defaultactions[i] == SIG_ACTION_IGN) || SIGNAL_GET(&thread->signals.mask, i))
			continue;

		if (SIGNAL_GET(&thread->signals.pending, i) || SIGNAL_GET(&thread->proc->signals.pending, i)) {
			gotsignal = true;
			break;
		}
	}

	if (sleeping && (thread->shouldexit || gotsignal) && (thread->flags & SCHED_THREAD_FLAGS_INTERRUPTIBLE)) {
		sleeping = false;
		next = NULL;
		thread->flags &= ~(SCHED_THREAD_FLAGS_SLEEP | SCHED_THREAD_FLAGS_INTERRUPTIBLE);
		thread->wakeupreason = SCHED_WAKEUP_REASON_INTERRUPTED;
		spinlock_release(&thread->sleeplock);
	}

	if (next || sleeping) {
		ARCH_CONTEXT_THREADSAVE(thread, context);

		thread->flags &= ~SCHED_THREAD_FLAGS_RUNNING;
		if (sleeping == false)
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
	arch_context_saveandcall(yield, _cpu()->schedulerstack, NULL);
	interrupt_set(old);
	return sleeping ? _cpu()->thread->wakeupreason : 0;
}

void sched_preparesleep(bool interruptible) {
	_cpu()->thread->sleepintstatus = interrupt_set(false);
	spinlock_acquire(&_cpu()->thread->sleeplock);
	// no locking needed as only we will be accessing it
	_cpu()->thread->flags |= SCHED_THREAD_FLAGS_SLEEP | (interruptible ? SCHED_THREAD_FLAGS_INTERRUPTIBLE : 0);
}

bool sched_wakeup(thread_t *thread, int reason) {
	bool intstate = interrupt_set(false);
	spinlock_acquire(&thread->sleeplock);

	if ((thread->flags & SCHED_THREAD_FLAGS_SLEEP) == 0 || ((reason == SCHED_WAKEUP_REASON_INTERRUPTED) && (thread->flags & SCHED_THREAD_FLAGS_INTERRUPTIBLE) == 0)) {
		spinlock_release(&thread->sleeplock);
		interrupt_set(intstate);
		return false;
	}

	thread->flags &= ~(SCHED_THREAD_FLAGS_SLEEP | SCHED_THREAD_FLAGS_INTERRUPTIBLE);
	thread->wakeupreason = reason;

	sched_queue(thread);
	spinlock_release(&thread->sleeplock);
	interrupt_set(intstate);

	return true;
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

static void setnodeslock(vnode_t **addr, vnode_t *new) {
	proc_t *proc = _cpu()->thread->proc;
	vnode_t *oldnode;
	VOP_HOLD(new);
	bool intstatus = interrupt_set(false);
	spinlock_acquire(&proc->nodeslock);
	oldnode = *addr;
	*addr = new;
	spinlock_release(&proc->nodeslock);
	interrupt_set(intstatus);
	VOP_RELEASE(oldnode);
}

void sched_setcwd(vnode_t *new) {
	proc_t *proc = _cpu()->thread->proc;
	setnodeslock(&proc->cwd, new);
}
void sched_setroot(vnode_t *new) {
	proc_t *proc = _cpu()->thread->proc;
	setnodeslock(&proc->root, new);
}

// once a scheduler dpc gets run, the return context is set to this function using the scheduler stack
static void dopreempt() {
	// interrupts are disabled, the thread context is already saved
	spinlock_acquire(&runqueuelock);

	thread_t *current = _cpu()->thread;
	thread_t *next = runqueuenext(current->priority);

	current->flags &= ~SCHED_THREAD_FLAGS_PREEMPTED;
	if (next) {
		current->flags &= ~SCHED_THREAD_FLAGS_RUNNING;
		runqueueinsert(current);
	} else {
		next = current;
	}

	spinlock_release(&runqueuelock);
	switchthread(next);
}

static void timerhook(context_t *context, dpcarg_t arg) {
	thread_t* current = _cpu()->thread;
	interrupt_set(false);

	// no need to preempt it again
	if (current->flags & SCHED_THREAD_FLAGS_PREEMPTED)
		return;

	current->flags |= SCHED_THREAD_FLAGS_PREEMPTED;
	ARCH_CONTEXT_THREADSAVE(current, context);

	CTX_INIT(context, false, false);
	CTX_SP(context) = (uintptr_t)_cpu()->schedulerstack;
	CTX_IP(context) = (uintptr_t)dopreempt;
}

static void cpuidlethread() {
	sched_targetcpu(_cpu());
	interrupt_set(true);
	while (1) {
		CPU_HALT();
		sched_yield();
	}
}

void sched_targetcpu(cpu_t *cpu) {
	bool intstatus = interrupt_set(false);
	_cpu()->thread->cputarget = cpu;
	interrupt_set(intstatus);
}

static void timeout(context_t *, dpcarg_t arg) {
	thread_t *thread = arg;
	sched_wakeup(thread, 0);
}

void sched_sleepus(size_t us) {
	timerentry_t sleepentry = {0};
	sched_preparesleep(false);

	timer_insert(_cpu()->timer, &sleepentry, timeout, _cpu()->thread, us, false);
	sched_yield();
}

void sched_apentry() {
	_cpu()->schedulerstack = vmm_map(NULL, SCHEDULER_STACK_SIZE, VMM_FLAGS_ALLOCATE, ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC, NULL);
	__assert(_cpu()->schedulerstack);
	_cpu()->schedulerstack = (void *)((uintptr_t)_cpu()->schedulerstack + SCHEDULER_STACK_SIZE);

	_cpu()->idlethread = sched_newthread(cpuidlethread, PAGE_SIZE * 4, 3, NULL, NULL);
	__assert(_cpu()->idlethread);

	timer_insert(_cpu()->timer, &_cpu()->schedtimerentry, timerhook, NULL, QUANTUM_US, true);
	timer_resume(_cpu()->timer);
	sched_stopcurrentthread();
}

void sched_init() {
	threadcache = slab_newcache(sizeof(thread_t), 0, NULL, NULL);
	__assert(threadcache);
	processcache = slab_newcache(sizeof(proc_t), 0, NULL, NULL);
	__assert(processcache);

	__assert(hashtable_init(&pidtable, 100) == 0);
	MUTEX_INIT(&sched_pidtablemutex);

	_cpu()->schedulerstack = vmm_map(NULL, SCHEDULER_STACK_SIZE, VMM_FLAGS_ALLOCATE, ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC, NULL);
	__assert(_cpu()->schedulerstack);
	_cpu()->schedulerstack = (void *)((uintptr_t)_cpu()->schedulerstack + SCHEDULER_STACK_SIZE);

	SPINLOCK_INIT(runqueuelock);

	_cpu()->idlethread = sched_newthread(cpuidlethread, PAGE_SIZE * 4, 3, NULL, NULL);
	__assert(_cpu()->idlethread);
	_cpu()->thread = sched_newthread(NULL, PAGE_SIZE * 32, 0, NULL, NULL);
	__assert(_cpu()->thread);

	timer_insert(_cpu()->timer, &_cpu()->schedtimerentry, timerhook, NULL, QUANTUM_US, true);
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
	__assert(vfs_open(vfsroot, "/init", 0, &initnode) == 0);

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

	vnode_t *consolenode;
	__assert(devfs_getbyname("console", &consolenode) == 0);
	__assert(VOP_OPEN(&consolenode, V_FFLAGS_READ | V_FFLAGS_NOCTTY, &proc->cred) == 0)
	VOP_HOLD(consolenode);
	__assert(VOP_OPEN(&consolenode, V_FFLAGS_WRITE | V_FFLAGS_NOCTTY, &proc->cred) == 0)
	VOP_HOLD(consolenode);
	__assert(VOP_OPEN(&consolenode, V_FFLAGS_WRITE | V_FFLAGS_NOCTTY, &proc->cred) == 0)

	file_t *stdin = fd_allocate();
	file_t *stdout = fd_allocate();
	file_t *stderr = fd_allocate();

	stdin->vnode = consolenode;
	stdout->vnode = consolenode;
	stderr->vnode = consolenode;

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

	char *argv[] = {"/init", cmdline_get("initarg"), NULL};
	char *envp[] = {NULL};

	void *stack = elf_preparestack(STACK_TOP, &auxv64, argv, envp);
	__assert(stack);

	// reenter kernel context
	vmm_switchcontext(&vmm_kernelctx);

	thread_t *uthread = sched_newthread(entry, PAGE_SIZE * 16, 1, proc, stack);
	__assert(uthread);

	proc->threads[0] = uthread;

	uthread->vmmctx = vmmctx;
	sched_queue(uthread);

	VOP_RELEASE(initnode);
	// proc starts with 1 refcount, release it here as to only have the thread reference
	PROC_RELEASE(proc);
}
