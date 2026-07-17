#ifndef _CPU_H
#define _CPU_H

#include <stddef.h>
#include <stdint.h>

#include <arch/msr.h>
#include <arch/ist.h>
#include <kernel/interrupt.h>
#include <kernel/mm.h>
#include <kernel/timer.h>
#include <kernel/scheduler.h>
#include <kernel/dpc.h>
#include <arch/apic.h>
#include <kernel/timekeeper.h>
#include <ringbuffer.h>

#define ARCH_EOI arch_apic_eoi

typedef struct cpu_t {
	// expected to be exposed to the rest of the kernel
	thread_t *thread; // expected to be here by other code

	struct cpu_t *self; // expected to be here by other code

	mm_context_t *mmctx; // expected to be here by other code

	long hardware_id; // expected to be here by other code
	long internal_id; // expected to be here by other code

	uint64_t scratch; // expected to be here by other code

	timekeeper_source_t *timekeeper_source;
	timekeeper_source_info_t *timekeeper_source_info;
	time_t timekeeper_source_base_ticks;
	time_t timekeeper_source_tick_offset;
	isr_t *timekeeper_sync_isr;

	isr_t isr[MAX_ISR_COUNT];
	bool intstatus;
	long ipl;
	isr_t *isrqueue;

	timer_t *timer;

	isr_t *dpcisr;
	dpc_t *dpcqueue;

	thread_t *idlethread;
	timerentry_t schedtimerentry;
	void *schedulerstack;

	dpc_t  reschedule_dpc;
	isr_t *reschedule_isr;

	sched_run_queue_t rt_queue;
	sched_calendar_queue_t ts_queue;
	sched_run_queue_t idle_queue;
	int last_interactivity;
	int last_queue;
	size_t thread_count;
	size_t stealable_thread_count;
	spinlock_t sched_lock;

	timerentry_t calendar_tick_timer_entry;

	void *topology_node;

	void *shootdown_page;
	size_t shootdown_size;
	spinlock_t shootdown_lock;
	int *shootdown_done;

#ifdef ENABLE_PROFILING
	ringbuffer_t *prof_ringbuffer;
#endif

	// architecture specific, does not need to be exposed

	uint64_t gdt[9];
	char vendor[13];
	ist_t ist;
	int acpiid;

	uint32_t cpuid_max;

	int topology_thread;
	int topology_core;
	int topology_package;
} cpu_t;

#define CPU_HALT() asm volatile("hlt")
#define CPU_PAUSE() asm volatile("pause")

bool load_seg_gs(int segment);
void load_seg_gs_fallback(void);
bool load_seg_fs(int segment);
void load_seg_fs_fallback(void);

static inline uint32_t cpu_to_be_d(uint32_t d) {
	return __builtin_bswap32(d);
}

static inline uint16_t cpu_to_be_w(uint16_t w) {
	return __builtin_bswap16(w);
}

static inline uint32_t be_to_cpu_d(uint32_t d) {
	return __builtin_bswap32(d);
}

static inline uint16_t be_to_cpu_w(uint16_t w) {
	return __builtin_bswap16(w);
}

cpu_t *get_bsp(void);
size_t cpu_cache_line_size(void);
void arch_cpu_user_access_begin(void);
void arch_cpu_user_access_end(void);
bool arch_cpu_smep_enabled(void);
bool arch_cpu_smap_enabled(void);

static inline thread_t *current_thread(void) {
	thread_t *thread;
	asm volatile ("mov %%gs:0, %%rax" : "=a"(thread) : : "memory");
	return thread;
}

static inline cpu_t *current_cpu(void) {
	cpu_t *cpu;
	asm volatile ("mov %%gs:8, %%rax" : "=a"(cpu) : : "memory");
	return cpu;
}

static inline mm_context_t *current_mm_context(void) {
	mm_context_t *context;
	asm volatile ("mov %%gs:16, %%rax" : "=a"(context) : : "memory");
	return context;
}

static inline void set_current_mm_context(mm_context_t *context) {
	asm volatile ("mov %%rax, %%gs:16" : : "a"(context) : "memory");
}

static inline long current_cpu_internal_id(void) {
	long id;
	asm volatile ("mov %%gs:32, %%rax" : "=a"(id) : : "memory");
	return id;
}

static inline long current_cpu_id(void) {
	long id;
	asm volatile ("mov %%gs:24, %%rax" : "=a"(id) : : "memory");
	return id;
}

static inline void cpu_set(cpu_t *ptr) {
	ptr->self = ptr;
	wrmsr(MSR_GSBASE, (uint64_t)ptr);
}

void arch_cpu_init();

#endif
