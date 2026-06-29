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
#include <kernel/auth.h>
#include <arch/smp.h>
#include <kernel/init.h>

#define QUANTUM_US 10000
#define SCHEDULER_STACK_SIZE PAGE_SIZE * 16

static __attribute__((noreturn)) void switch_thread(thread_t *thread) {
	interrupt_set(false);
	thread_t* current = current_thread();

	sched_thread_running_callback(thread);

	current_cpu()->thread = thread;

	if(current == NULL || thread->vmmctx != current->vmmctx)
		vmm_switchcontext(thread->vmmctx);

	current_cpu()->intstatus = ARCH_CONTEXT_INTSTATUS(&thread->context);
	thread->cpu = current_cpu();
	if (current)
		current->flags &= ~THREAD_FLAGS_RUNNING;

	__assert((thread->flags & THREAD_FLAGS_RUNNING) == 0);

	if (current && current->flags & THREAD_FLAGS_SLEEP)
		spinlock_release(&current->sleeplock);

	thread->flags |= THREAD_FLAGS_RUNNING;
	__assert((thread->flags & THREAD_FLAGS_QUEUED) == 0);

	void *schedulerstack = current_cpu()->schedulerstack;
	__assert(!((void *)thread->context.rsp < schedulerstack && (void *)thread->context.rsp >= (schedulerstack - SCHEDULER_STACK_SIZE)));

	ARCH_CONTEXT_SWITCHTHREAD(thread);
	__builtin_unreachable();
}

// no metrics callback here as this will be called from things like threads stopping
__attribute__((noreturn)) void sched_stop_current_thread() {
	interrupt_set(false);

	if (current_thread())
		current_thread()->flags &= ~THREAD_FLAGS_RUNNING;

	spinlock_acquire(&current_cpu()->sched_lock);
	thread_t *next = sched_select_next_thread();
	spinlock_release(&current_cpu()->sched_lock);

	__assert(next);

	switch_thread(next);
}

typedef struct {
	context_t *context;
	bool syscall;
	uint64_t syscallerrno;
	uint64_t syscallret;
} checkargs_t;

// SHOULD NOT BE CALLED WITH THE SCHEDULER STACK
static bool userspacecheck(void *_args) {
	checkargs_t *args = _args;
	thread_t *thread = current_thread();

	if (thread->shouldexit) {
		interrupt_set(true);
		sched_threadexit();
	}

	bool need_context_switch = false;

	while (signal_check(thread, args->context, args->syscall, args->syscallret, args->syscallerrno, &need_context_switch)) ;
	return need_context_switch;
}

static void checktrampoline(context_t *context, void *_args) {
	__assert(ARCH_CONTEXT_INTSTATUS(context) == false);
	checkargs_t args = *(checkargs_t *)_args;
	userspacecheck(&args);
	current_cpu()->intstatus = ARCH_CONTEXT_INTSTATUS(args.context);
	arch_context_switch(args.context);
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

	bool sleeping = thread->flags & THREAD_FLAGS_SLEEP;

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
		thread->flags &= ~(THREAD_FLAGS_SLEEP | THREAD_FLAGS_INTERRUPTIBLE);
		thread->wakeupreason = SCHED_WAKEUP_REASON_INTERRUPTED;
		spinlock_release(&thread->sleeplock);
	}

	spinlock_acquire(&current_cpu()->sched_lock);

	thread->flags &= ~THREAD_FLAGS_RUNNING;

	if (sleeping == false) {
		sched_insert_in_cpu_queue(current_cpu(), thread);
	}

	thread_t *next = sched_select_next_thread();

	if (thread == next)
		thread->flags |= THREAD_FLAGS_RUNNING;

	spinlock_release(&current_cpu()->sched_lock);

	if (next != thread || sleeping) {
		ARCH_CONTEXT_THREADSAVE(thread, context);

		switch_thread(next);
	}

	sched_thread_running_callback(current_thread());
}

int sched_yield() {
	bool sleeping = current_thread()->flags & THREAD_FLAGS_SLEEP;
	bool old = sleeping ? current_thread()->sleepintstatus : interrupt_set(false);

	sched_thread_stopping_callback(current_thread(), sleeping);
	arch_context_saveandcall(yield, current_cpu()->schedulerstack, NULL);

	__assert(current_cpu()->ipl == IPL_NORMAL);
	interrupt_set(old);
	return sleeping ? current_thread()->wakeupreason : 0;
}

void sched_prepare_sleep(bool interruptible) {
	current_thread()->sleepintstatus = interrupt_set(false);
	spinlock_acquire(&current_thread()->sleeplock);
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

	sched_thread_wakeup_callback(thread);

	sched_queue(thread);
	spinlock_release(&thread->sleeplock);
	interrupt_set(intstate);

	return true;
}

// once a scheduler dpc gets run, the return context is set to this function using the scheduler stack
static void dopreempt() {
	// interrupts are disabled, the thread context is already saved
	thread_t *current = current_thread();

	spinlock_acquire(&current_cpu()->sched_lock);

	current->flags &= ~(THREAD_FLAGS_RUNNING | THREAD_FLAGS_PREEMPTED);
	sched_insert_in_cpu_queue(current_cpu(), current);
	thread_t *next = sched_select_next_thread();

	spinlock_release(&current_cpu()->sched_lock);

	switch_thread(next);
}

static void sched_reschedule_dpc(context_t *context, dpcarg_t arg) {
	thread_t* current = current_thread();
	interrupt_set(false);

	// no need to preempt it again
	if (current->flags & THREAD_FLAGS_PREEMPTED)
		return;

	sched_thread_stopping_callback(current, false);

	current->flags |= THREAD_FLAGS_PREEMPTED;
	ARCH_CONTEXT_THREADSAVE(current, context);

	CTX_INIT(context, false, false);
	CTX_SP(context) = (uintptr_t)current_cpu()->schedulerstack;
	CTX_IP(context) = (uintptr_t)dopreempt;
}

// IPL_DPC
static void reschedule_timer_dpc(context_t *context, dpcarg_t arg) {
	dpc_enqueue(&current_cpu()->reschedule_dpc, NULL);
}

// IPL_MAX
static void reschedule_ipi(isr_t *, context_t *) {
	dpc_enqueue(&current_cpu()->reschedule_dpc, NULL);
}

void sched_preempt_cpu(cpu_t *cpu) {
	long ipl = interrupt_raiseipl(IPL_DPC);
	if (cpu == current_cpu()) {
		dpc_enqueue(&cpu->reschedule_dpc, NULL);
	} else {
		arch_smp_send_ipi(cpu, cpu->reschedule_isr, ARCH_SMP_IPI_TARGET, false);
	}
	interrupt_loweripl(ipl);
}

static void idle_thread(void) {
	interrupt_set(true);
	while (1)
		CPU_HALT();
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

	ARCH_CONTEXT_THREADSAVE(thread, context);

	spinlock_acquire(&cpu->sched_lock);
	thread->flags &= ~THREAD_FLAGS_RUNNING;
	sched_insert_in_cpu_queue(cpu, thread);
	spinlock_release(&cpu->sched_lock);

	spinlock_acquire(&current_cpu()->sched_lock);
	thread_t *next = sched_select_next_thread();

	if (sched_thread_can_run_in_cpu(thread, cpu->last_queue, cpu->last_interactivity))
		arch_smp_send_ipi(cpu, cpu->reschedule_isr, ARCH_SMP_IPI_TARGET, false);

	spinlock_release(&current_cpu()->sched_lock);
	switch_thread(next);
}

// TODO verify if something like this is really needed
void sched_reschedule_on_cpu(cpu_t *cpu, bool target) {
	cpu_t *old_target = current_thread()->cputarget;
	current_thread()->cputarget = cpu;

	bool status = interrupt_set(false);

	// already on the cpu
	if (cpu == current_cpu())
		goto leave;

	sched_thread_stopping_callback(current_thread(), false);

	arch_context_saveandcall(reschedule_yield, current_cpu()->schedulerstack, cpu);

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

static void set_up_bitmaps(void) {
	__assert(bitmap_init(&current_cpu()->rt_queue.thread_bitmap, SCHED_RUN_QUEUE_SIZE) == 0);
	__assert(bitmap_init(&current_cpu()->idle_queue.thread_bitmap, SCHED_RUN_QUEUE_SIZE) == 0);
}

void sched_calendar_tick(context_t *, dpcarg_t);
void sched_load_balancer(context_t *, dpcarg_t);

void sched_ap_entry() {
	SPINLOCK_INIT(current_cpu()->sched_lock);

	dpc_prepare(&current_cpu()->reschedule_dpc, sched_reschedule_dpc);
	set_up_bitmaps();

	current_cpu()->schedulerstack = vmm_map(NULL, SCHEDULER_STACK_SIZE, VMM_FLAGS_ALLOCATE, ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC, NULL);
	__assert(current_cpu()->schedulerstack);
	current_cpu()->schedulerstack = (void *)((uintptr_t)current_cpu()->schedulerstack + SCHEDULER_STACK_SIZE);

	current_cpu()->idlethread = sched_newthread(idle_thread, PAGE_SIZE * 4, 100, NULL, NULL);
	__assert(current_cpu()->idlethread);
	current_cpu()->idlethread->cputarget = current_cpu();
	current_cpu()->idlethread->class = THREAD_CLASS_IDLE;
	sched_queue(current_cpu()->idlethread);

	current_cpu()->reschedule_isr = interrupt_allocate(reschedule_ipi, ARCH_EOI, IPL_MAX);
	__assert(current_cpu()->reschedule_isr);

	timer_insert(current_cpu()->timer, &current_cpu()->schedtimerentry, reschedule_timer_dpc, NULL, QUANTUM_US, true);
	timer_insert(current_cpu()->timer, &current_cpu()->calendar_tick_timer_entry, sched_calendar_tick, NULL, 10000, true);
	timer_resume(current_cpu()->timer);
	sched_stop_current_thread();
}

void sched_init() {
	static timerentry_t sched_load_balancer_entry;
	proc_init();

	SPINLOCK_INIT(current_cpu()->sched_lock);
	SPINLOCK_INIT(sched_idle_cpu_bitmap_lock);

	dpc_prepare(&current_cpu()->reschedule_dpc, sched_reschedule_dpc);
	set_up_bitmaps();

	__assert(bitmap_init(&sched_idle_cpu_bitmap, arch_smp_get_cpu_count()) == 0);

	current_cpu()->schedulerstack = vmm_map(NULL, SCHEDULER_STACK_SIZE, VMM_FLAGS_ALLOCATE, ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC, NULL);
	__assert(current_cpu()->schedulerstack);
	current_cpu()->schedulerstack = (void *)((uintptr_t)current_cpu()->schedulerstack + SCHEDULER_STACK_SIZE);

	current_cpu()->idlethread = sched_newthread(idle_thread, PAGE_SIZE * 4, 100, NULL, NULL);
	__assert(current_cpu()->idlethread);
	current_cpu()->idlethread->cputarget = current_cpu();
	current_cpu()->idlethread->class = THREAD_CLASS_IDLE;

	current_cpu()->thread = sched_newthread(NULL, PAGE_SIZE * 32, 0, NULL, NULL);
	__assert(current_thread());

	sched_queue(current_cpu()->idlethread);

	current_cpu()->reschedule_isr = interrupt_allocate(reschedule_ipi, ARCH_EOI, IPL_MAX);
	__assert(current_cpu()->reschedule_isr);

	sched_target_cpu(current_cpu());

	sched_thread_running_callback(current_thread());

	timer_insert(current_cpu()->timer, &current_cpu()->schedtimerentry, reschedule_timer_dpc, NULL, QUANTUM_US, true);
	timer_insert(current_cpu()->timer, &current_cpu()->calendar_tick_timer_entry, sched_calendar_tick, NULL, 10000, true);
	// TODO randomize interval
	timer_insert(current_cpu()->timer, &sched_load_balancer_entry, sched_load_balancer, NULL, 1000000, true);
	// XXX move this resume to a more appropriate place
	timer_resume(current_cpu()->timer);
}

INIT_ROUTINE_DEFINE(scheduler, INIT_ROUTINE_FLAGS_NONE, sched_init, arch_timer);
