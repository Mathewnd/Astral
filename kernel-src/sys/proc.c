#include <kernel/proc.h>
#include <hashtable.h>
#include <kernel/jobctl.h>
#include <kernel/auth.h>
#include <kernel/alloc.h>
#include <kernel/slab.h>
#include <kernel/file.h>

static hashtable_t pid_table;
static scache_t *processcache;

// not defined as static because its acquired/released from a macro in kernel/proc.h
mutex_t proc_pid_table_mutex;

proc_t *init_proc;
static pid_t currpid = 1;

pid_t proc_allocate_pid(void) {
	return __atomic_fetch_add(&currpid, 1, __ATOMIC_SEQ_CST);
}

proc_t *proc_get_from_pid(int pid) {
	MUTEX_ACQUIRE(&proc_pid_table_mutex, false);
	void *_proc = NULL;
	hashtable_get(&pid_table, &_proc, &pid, sizeof(pid));
	proc_t *proc = _proc;
	if (proc) {
		PROC_HOLD(proc);
	}
	MUTEX_RELEASE(&proc_pid_table_mutex);

	return proc;
}

int proc_signal_all(int signal, proc_t *sender) {
	MUTEX_ACQUIRE(&proc_pid_table_mutex, false);

	pid_t senderpgid = sender ? jobctl_getpgid(sender) : 0;

	size_t donecount = 0;
	HASHTABLE_FOREACH(&pid_table) {
		proc_t *current = entry->value;
		// init does not get signaled
		if (current->pid == 1)
			continue;

		pid_t currentpgid = jobctl_getpgid(current);

		if (senderpgid == 0 || (signal == SIGCONT && currentpgid == senderpgid) || auth_process_check(&sender->cred, AUTH_ACTIONS_PROCESS_SIGNAL, current) == 0) {
			++donecount;
			signal_signalproc(current, signal);
		}
	}

	MUTEX_RELEASE(&proc_pid_table_mutex);
	return donecount == 0 ? EPERM : 0;
}

static void rtdpc(context_t *, dpcarg_t arg) {
	signal_signalproc(arg, SIGALRM);
}

static void vtdpc(context_t *, dpcarg_t arg) {
	signal_signalproc(arg, SIGVTALRM);
}

static void profdpc(context_t *, dpcarg_t arg) {
	signal_signalproc(arg, SIGPROF);
}

proc_t *proc_create() {
	proc_t *proc = slab_allocate(processcache);
	if (proc == NULL)
		return NULL;

	memset(proc, 0, sizeof(proc_t));

	// TODO move this to slab
	proc->state = SCHED_PROC_STATE_NORMAL;
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
	MUTEX_INIT(&proc->timer.mutex);
	itimer_init(&proc->timer.realtime, rtdpc, proc);
	itimer_init(&proc->timer.virtualtime, vtdpc, proc);
	itimer_init(&proc->timer.profiling, profdpc, proc);
	SPINLOCK_INIT(proc->threadlistlock);

	proc->fd = alloc(sizeof(fd_t) * 3);
	if (proc->fd == NULL) {
		slab_free(processcache, proc);
		return NULL;
	}

	proc->pid = proc_allocate_pid();
	proc->umask = 022; // default umask

	// add to pid table
	MUTEX_ACQUIRE(&proc_pid_table_mutex, false);
	if (hashtable_set(&pid_table, proc, &proc->pid, sizeof(proc->pid), true)) {
		MUTEX_RELEASE(&proc_pid_table_mutex);
		free(proc->fd);
		slab_free(processcache, proc);
		return NULL;
	}
	MUTEX_RELEASE(&proc_pid_table_mutex);

	return proc;
}

void proc_destroy(proc_t *proc) {
	free(proc->fd);
	slab_free(processcache, proc);
}

void proc_inactive(proc_t *proc) {
	// proc refcount 0, we can free the structure
	// the pid table lock is already acquired.

	hashtable_remove(&pid_table, &proc->pid, sizeof(proc->pid));

	//arch_e9_puts("\n\ndestroy proc\n\n");
	//printf("destroy proc\n");
	proc_destroy(proc);
}

void proc_stop_other_threads() {
	thread_t *thread = current_thread();
	proc_t *proc = thread->proc;

	bool intstatus = interrupt_set(false);
	spinlock_acquire(&proc->threadlistlock);

	proc->nomorethreads = true;
	thread_t *threadlist = proc->threadlist;

	spinlock_release(&proc->threadlistlock);
	interrupt_set(intstatus);

	while (threadlist) {
		if (threadlist == thread) {
			threadlist = threadlist->procnext;
			continue;
		}

		threadlist->shouldexit = true;
		sched_wakeup(threadlist, SCHED_WAKEUP_REASON_INTERRUPTED);
		threadlist = threadlist->procnext;
	}

	while (__atomic_load_n(&proc->runningthreadcount, __ATOMIC_SEQ_CST) > 1) {
		arch_e9_puts("proc_stop_other_threads\n");
		sched_yield();
	}

	proc->nomorethreads = false;
}

void proc_terminate(int status) {
	thread_t *thread = current_thread();
	proc_t *proc = thread->proc;
	if (spinlock_try(&proc->exiting) == false)
		sched_threadexit();

	proc_stop_other_threads();

	proc->status = status;

	sched_threadexit();
}

static vnode_t *getnodeslock(vnode_t **addr) {
	proc_t *proc = current_thread()->proc;
	bool intstatus = interrupt_set(false);
	spinlock_acquire(&proc->nodeslock);
	vnode_t *vnode = *addr;
	VOP_HOLD(vnode);
	spinlock_release(&proc->nodeslock);
	interrupt_set(intstatus);
	return vnode;
}

vnode_t *proc_get_cwd() {
	proc_t *proc = current_thread()->proc;
	return getnodeslock(&proc->cwd);
}

vnode_t *proc_get_root() {
	proc_t *proc = current_thread()->proc;
	return getnodeslock(&proc->root);
}

static void setnodeslock(vnode_t **addr, vnode_t *new) {
	proc_t *proc = current_thread()->proc;
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

void proc_set_cwd(vnode_t *new) {
	proc_t *proc = current_thread()->proc;
	setnodeslock(&proc->cwd, new);
}

void proc_set_root(vnode_t *new) {
	proc_t *proc = current_thread()->proc;
	setnodeslock(&proc->root, new);
}

void proc_init(void) {
	processcache = slab_newcache(sizeof(proc_t), 0, NULL, NULL);
	__assert(processcache);

	__assert(hashtable_init(&pid_table, 100) == 0);
	MUTEX_INIT(&proc_pid_table_mutex);
}
