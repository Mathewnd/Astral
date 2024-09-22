#include <kernel/scheduler.h>
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
#include <kernel/auth.h>
#include <arch/smp.h>

#define QUANTUM_US 100000
#define SCHEDULER_STACK_SIZE PAGE_SIZE * 16

#define RUNQUEUE_COUNT 64

typedef struct {
	thread_t *list;
	thread_t *last;
} rqueue_t;

static rqueue_t runqueue[RUNQUEUE_COUNT];
static uint64_t runqueuebitmap;
static spinlock_t runqueuelock;

static thread_t *getinrunqueue(rqueue_t *rq) {
	thread_t *thread = rq->list;

	while (thread) {
		if (thread->cputarget == NULL || thread->cputarget == current_cpu())
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
		thread->flags &= ~THREAD_FLAGS_QUEUED;

	leave:
	interrupt_set(intstate);
	return thread;
}

static __attribute__((noreturn)) void switch_thread(thread_t *thread) {
	interrupt_set(false);
	thread_t* current = current_thread();
	
	current_cpu()->thread = thread;

	if(current == NULL || thread->vmmctx != current->vmmctx)
		vmm_switchcontext(thread->vmmctx);

	current_cpu()->intstatus = ARCH_CONTEXT_INTSTATUS(&thread->context);
	thread->cpu = current_cpu();
	// XXX make the locking of the flags field better, candidate for a r/w lock
	spinlock_acquire(&runqueuelock);
	if (current)
		current->flags &= ~THREAD_FLAGS_RUNNING;

	__assert((thread->flags & THREAD_FLAGS_RUNNING) == 0);

	if (current && current->flags & THREAD_FLAGS_SLEEP)
		spinlock_release(&current->sleeplock);

	thread->flags |= THREAD_FLAGS_RUNNING;
	__assert((thread->flags & THREAD_FLAGS_QUEUED) == 0);
	spinlock_release(&runqueuelock);

	void *schedulerstack = current_cpu()->schedulerstack;
	__assert(!((void *)thread->context.rsp < schedulerstack && (void *)thread->context.rsp >= (schedulerstack - SCHEDULER_STACK_SIZE)));

	ARCH_CONTEXT_SWITCHTHREAD(thread);
	__builtin_unreachable();
}

static void runqueueinsert(thread_t *thread) {
	__assert((thread->flags & THREAD_FLAGS_RUNNING) == 0);
	thread->flags |= THREAD_FLAGS_QUEUED;

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
	__assert((thread->flags & THREAD_FLAGS_QUEUED) == 0 && (thread->flags & THREAD_FLAGS_RUNNING) == 0);

	runqueueinsert(thread);

	spinlock_release(&runqueuelock);
	interrupt_set(intstate);
	// TODO yield if higher priority than current thread (or send another CPU an IPI)
}

__attribute__((noreturn)) void sched_stop_current_thread() {
	interrupt_set(false);

	spinlock_acquire(&runqueuelock);
	if (current_thread())
		current_thread()->flags &= ~THREAD_FLAGS_RUNNING;

	thread_t *next = runqueuenext(0x0fffffff);
	if (next == NULL)
		next = current_cpu()->idlethread;

	spinlock_release(&runqueuelock);

	switch_thread(next);
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
	thread_t *thread = current_thread();

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
	current_cpu()->intstatus = ARCH_CONTEXT_INTSTATUS(args->context);
	arch_context_switch(args->context);
}

// called right before going back to userspace in the syscall handler, interrupt handler and arch_context_switch
__attribute__((no_caller_saved_registers)) void sched_userspacecheck(context_t *context, bool syscall, uint64_t syscallerrno, uint64_t syscallret) {
	__assert(current_cpu());
	if (current_thread() == NULL || ARCH_CONTEXT_ISUSER(context) == false)
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
	if (&args < (checkargs_t *)current_cpu()->schedulerstack && &args >= (checkargs_t *)((uintptr_t)current_cpu()->schedulerstack - SCHEDULER_STACK_SIZE)) {
		arch_context_saveandcall(checktrampoline, current_thread()->kernelstacktop, &args);
	} else {
		userspacecheck(&args);
	}

	current_cpu()->intstatus = intstatus;
}

static void yield(context_t *context, void *) {
	thread_t *thread = current_thread();

	spinlock_acquire(&runqueuelock);

	bool sleeping = thread->flags & THREAD_FLAGS_SLEEP;

	thread_t *next = runqueuenext(sleeping ? 0x0fffffff : thread->priority);
	bool gotsignal = false;
	for (int i = 1; i < NSIG && thread->proc; ++i) {
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

	if (sleeping && (thread->shouldexit || gotsignal) && (thread->flags & THREAD_FLAGS_INTERRUPTIBLE)) {
		sleeping = false;

		if (next)
			runqueueinsert(next);
		next = NULL;

		thread->flags &= ~(THREAD_FLAGS_SLEEP | THREAD_FLAGS_INTERRUPTIBLE);
		thread->wakeupreason = SCHED_WAKEUP_REASON_INTERRUPTED;
		spinlock_release(&thread->sleeplock);
	}

	if (next || sleeping) {
		ARCH_CONTEXT_THREADSAVE(thread, context);

		thread->flags &= ~THREAD_FLAGS_RUNNING;
		if (sleeping == false)
			runqueueinsert(thread);

		if (next == NULL)
			next = current_cpu()->idlethread;

		spinlock_release(&runqueuelock);
		switch_thread(next);
	}

	spinlock_release(&runqueuelock);
}

int sched_yield() {
	bool sleeping = current_thread()->flags & THREAD_FLAGS_SLEEP;
	bool old = sleeping ? current_thread()->sleepintstatus : interrupt_set(false);
	arch_context_saveandcall(yield, current_cpu()->schedulerstack, NULL);
	__assert(current_cpu()->ipl == IPL_NORMAL);
	interrupt_set(old);
	return sleeping ? current_thread()->wakeupreason : 0;
}

void sched_prepare_sleep(bool interruptible) {
	current_thread()->sleepintstatus = interrupt_set(false);
	spinlock_acquire(&current_thread()->sleeplock);
	// no locking needed as only we will be accessing it
	current_thread()->flags |= THREAD_FLAGS_SLEEP | (interruptible ? THREAD_FLAGS_INTERRUPTIBLE : 0);
}

bool sched_wakeup(thread_t *thread, int reason) {
	bool intstate = interrupt_set(false);
	spinlock_acquire(&thread->sleeplock);

	if ((thread->flags & THREAD_FLAGS_SLEEP) == 0 || ((reason == SCHED_WAKEUP_REASON_INTERRUPTED) && (thread->flags & THREAD_FLAGS_INTERRUPTIBLE) == 0)) {
		spinlock_release(&thread->sleeplock);
		interrupt_set(intstate);
		return false;
	}

	thread->flags &= ~(THREAD_FLAGS_SLEEP | THREAD_FLAGS_INTERRUPTIBLE);
	thread->wakeupreason = reason;

	// TODO send IPI to idle cores
	sched_queue(thread);
	spinlock_release(&thread->sleeplock);
	interrupt_set(intstate);

	return true;
}

// once a scheduler dpc gets run, the return context is set to this function using the scheduler stack
static void dopreempt() {
	// interrupts are disabled, the thread context is already saved
	spinlock_acquire(&runqueuelock);

	thread_t *current = current_thread();
	thread_t *next = runqueuenext(current->priority);

	current->flags &= ~THREAD_FLAGS_PREEMPTED;
	if (next) {
		current->flags &= ~THREAD_FLAGS_RUNNING;
		runqueueinsert(current);
	} else {
		next = current;
	}

	spinlock_release(&runqueuelock);
	switch_thread(next);
}

static void preempt_dpc(context_t *context, dpcarg_t arg) {
	thread_t* current = current_thread();
	interrupt_set(false);

	// no need to preempt it again
	if (current->flags & THREAD_FLAGS_PREEMPTED)
		return;

	current->flags |= THREAD_FLAGS_PREEMPTED;
	ARCH_CONTEXT_THREADSAVE(current, context);

	CTX_INIT(context, false, false);
	CTX_SP(context) = (uintptr_t)current_cpu()->schedulerstack;
	CTX_IP(context) = (uintptr_t)dopreempt;
}

// IPL_DPC
static void reschedule_timer_dpc(context_t *context, dpcarg_t arg) {
	dpc_enqueue(&current_cpu()->reschedule_dpc, preempt_dpc, NULL);
}

// IPL_MAX
static void reschedule_ipi(isr_t *, context_t *) {
	dpc_enqueue(&current_cpu()->reschedule_dpc, preempt_dpc, NULL);
}

static void cpuidlethread() {
	sched_target_cpu(current_cpu());
	interrupt_set(true);
	while (1) {
		CPU_HALT();
		sched_yield();
	}
}

void sched_target_cpu(cpu_t *cpu) {
	bool intstatus = interrupt_set(false);
	current_thread()->cputarget = cpu;
	interrupt_set(intstatus);
}

// yields the current thread and sends an reschedule ipi to a specific cpu
static void reschedule_yield(context_t *context, void *_cpu) {
	thread_t *thread = current_thread();
	cpu_t *cpu = _cpu;

	spinlock_acquire(&runqueuelock);

	thread_t *next = runqueuenext(0x0fffffff);

	ARCH_CONTEXT_THREADSAVE(thread, context);

	thread->flags &= ~THREAD_FLAGS_RUNNING;
	runqueueinsert(thread);

	if (next == NULL)
		next = current_cpu()->idlethread;

	arch_smp_sendipi(cpu, cpu->reschedule_isr, ARCH_SMP_IPI_TARGET, false);

	spinlock_release(&runqueuelock);
	switch_thread(next);
}

void sched_reschedule_on_cpu(cpu_t *cpu, bool target) {
	cpu_t *old_target = current_thread()->cputarget;
	current_thread()->cputarget = cpu;

	bool status = interrupt_set(false);

	// already on the cpu
	if (cpu == current_cpu())
		goto leave;

	long old_priority = current_thread()->priority;
	current_thread()->priority = 0;

	arch_context_saveandcall(reschedule_yield, current_cpu()->schedulerstack, cpu);

	current_thread()->priority = old_priority;

	leave:
	interrupt_set(status);
	if (target == false)
		current_thread()->cputarget = old_target;
}

static void timeout(context_t *, dpcarg_t arg) {
	thread_t *thread = arg;
	sched_wakeup(thread, 0);
}

void sched_sleep_us(size_t us) {
	timerentry_t sleepentry = {0};
	sched_prepare_sleep(false);

	timer_insert(current_cpu()->timer, &sleepentry, timeout, current_thread(), us, false);
	sched_yield();
}

void sched_ap_entry() {
	current_cpu()->schedulerstack = vmm_map(NULL, SCHEDULER_STACK_SIZE, VMM_FLAGS_ALLOCATE, ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC, NULL);
	__assert(current_cpu()->schedulerstack);
	current_cpu()->schedulerstack = (void *)((uintptr_t)current_cpu()->schedulerstack + SCHEDULER_STACK_SIZE);

	current_cpu()->idlethread = sched_newthread(cpuidlethread, PAGE_SIZE * 4, 3, NULL, NULL);
	__assert(current_cpu()->idlethread);

	current_cpu()->reschedule_isr = interrupt_allocate(reschedule_ipi, ARCH_EOI, IPL_MAX);
	__assert(current_cpu()->reschedule_isr);

	timer_insert(current_cpu()->timer, &current_cpu()->schedtimerentry, reschedule_timer_dpc, NULL, QUANTUM_US, true);
	timer_resume(current_cpu()->timer);
	sched_stop_current_thread();
}

void sched_init() {
	proc_init();

	current_cpu()->schedulerstack = vmm_map(NULL, SCHEDULER_STACK_SIZE, VMM_FLAGS_ALLOCATE, ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC, NULL);
	__assert(current_cpu()->schedulerstack);
	current_cpu()->schedulerstack = (void *)((uintptr_t)current_cpu()->schedulerstack + SCHEDULER_STACK_SIZE);

	SPINLOCK_INIT(runqueuelock);

	current_cpu()->idlethread = sched_newthread(cpuidlethread, PAGE_SIZE * 4, 3, NULL, NULL);
	__assert(current_cpu()->idlethread);
	current_cpu()->thread = sched_newthread(NULL, PAGE_SIZE * 32, 0, NULL, NULL);
	__assert(current_thread());

	current_cpu()->reschedule_isr = interrupt_allocate(reschedule_ipi, ARCH_EOI, IPL_MAX);
	__assert(current_cpu()->reschedule_isr);

	timer_insert(current_cpu()->timer, &current_cpu()->schedtimerentry, reschedule_timer_dpc, NULL, QUANTUM_US, true);
	// XXX move this resume to a more appropriate place
	timer_resume(current_cpu()->timer);
}
