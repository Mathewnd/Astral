#include <arch/cpu.h>
#include <arch/apic.h>
#include <cpuid.h>
#include <logging.h>
#include <arch/cpuid.h>
#include <kernel/topology.h>
#include <kernel/init.h>
#include <arch/idt.h>
#include <arch/gdt.h>
#include <kernel/dpc.h>
#include <arch/context.h>
#include <arch/prof.h>

#define EFER_SYSCALLENABLE 1

void arch_syscall_entry();
static void illisr(isr_t *self, context_t *ctx) {
	if (ARCH_CONTEXT_ISUSER(ctx)) {
		signal_signalthread(current_thread(), SIGILL, true);
	} else {
		_panic("Invalid Opcode", ctx);
	}
}

static void sigfpeisr(isr_t *self, context_t *ctx) {
	if (ARCH_CONTEXT_ISUSER(ctx)) {
		signal_signalthread(current_thread(), SIGFPE, true);
	} else {
		_panic("Division by 0", ctx);
	}
}

static void simdisr(isr_t *self, context_t *ctx) {
	if (ARCH_CONTEXT_ISUSER(ctx)) {
		signal_signalthread(current_thread(), SIGFPE, true);
	} else {
		_panic("SIMD Exception", ctx);
	}
}

static void x87isr(isr_t *self, context_t *ctx) {
	if (ARCH_CONTEXT_ISUSER(ctx)) {
		signal_signalthread(current_thread(), SIGFPE, true);
	} else {
		_panic("x87 Floating-Point Exception", ctx);
	}
}

static void dbisr(isr_t *self, context_t *ctx) {
	if (ARCH_CONTEXT_ISUSER(ctx)) {
		signal_signalthread(current_thread(), SIGTRAP, true);
	} else {
		_panic("Debug Exception", ctx);
	}
}

static void reload_ldt(isr_t *self, context_t *ctx) {
	if (current_thread() == NULL || current_thread()->proc == NULL || current_thread()->proc->ldt == NULL)
		return;

	arch_ldt_invalidate();
}

void arch_nmi_isr(context_t *ctx) {
	// we are in an NMI context, running on IST0 
	// first load the proper gsbase to get access to cpu-local variables. 
	// the pointer to cpu's data is at the beggining of the stack so after the context struct.
	uint64_t old_gsbase = rdmsr(MSR_GSBASE);
	wrmsr(MSR_GSBASE, *(uint64_t *)(ctx + 1));

	bool handled = false;

#ifdef ENABLE_PROFILING
	if (arch_profiling_irq(ctx))
		handled = true;
#endif

	if (!handled)
		_panic("Unhandled NMI", ctx);

	// restore the old gsbase
	wrmsr(MSR_GSBASE, old_gsbase);
}

#define TOPOLOGY_TYPE_THREAD 1
#define TOPOLOGY_TYPE_CORE   2
#define TOPOLOGY_TYPE_PACKAGE 0xffffffff

static size_t get_topology_depth(void) {
	bool is_amd = strcmp(current_cpu()->vendor, CPUID_VENDOR_AMD) == 0;
	bool is_intel = strcmp(current_cpu()->vendor, CPUID_VENDOR_INTEL) == 0;
	unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;

	__get_cpuid(1, &eax, &ebx, &ecx, &edx);
	bool has_package_bits = edx & CPUID_LEAF_1_EDX_HTT;

	if (is_intel && cpuid_leaf_0xb_available()) {
		int current_type;
		size_t depth = 0;
		for (;;) {
			__get_cpuid_count(0xb, depth, &eax, &ebx, &ecx, &edx);
			current_type = (ecx >> 8) & 0xff;
			if (current_type == 0)
				break;
			depth += 1;
		}

		__assert(depth >= 2); // intel guarantees that at least the thread and cpu level will exist
		return depth + 1; // account for package level
	} else if ((is_intel || is_amd) && has_package_bits) {
		return 3; // use the package -> core -> thread topology for this CPU
	} else {
		return 1; // it will be a leaf node in the root of the topology tree
	}
}

// last value in topology is supposed to be the leaf
static void get_topology(long *topology_ids, long *topology_types) {
	bool is_amd = strcmp(current_cpu()->vendor, CPUID_VENDOR_AMD) == 0;
	bool is_intel = strcmp(current_cpu()->vendor, CPUID_VENDOR_INTEL) == 0;
	unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
	size_t depth = get_topology_depth();

	__get_cpuid(1, &eax, &ebx, &ecx, &edx);
	bool has_package_bits = edx & CPUID_LEAF_1_EDX_HTT;

	uint8_t initial_apic_id = (ebx >> 24) & 0xff;

	size_t bits_below_package = 32 - __builtin_clz(((ebx >> 16) & 0xff) - 1);

	if (is_intel && cpuid_leaf_0xb_available()) {
		uint32_t bits = 0;
		for (int i = 0; i < depth - 1; ++i) {
			__get_cpuid_count(0xb, i, &eax, &ebx, &ecx, &edx);
			initial_apic_id = edx;
			uint32_t current_bits = eax & 0x1f;
			topology_ids[depth - 1 - i] = (initial_apic_id >> bits) & ((1 << current_bits) - 1);
			bits += current_bits;
		}

		topology_ids[0] = (long)(initial_apic_id >> bits) & ((1l << (32 - bits)) - 1);
		topology_types[0] = TOPOLOGY_TYPE_PACKAGE;
	} else if (is_intel && has_package_bits && cpuid_leaf_0x4_available()) {
		__get_cpuid_count(0x4, 0, &eax, &ebx, &ecx, &edx);

		size_t bits_for_core = 32 - __builtin_clz(((eax >> 26) & 0x3f));
		size_t bits_for_thread = bits_below_package - bits_for_core;
		uint32_t core_mask = (1 << bits_for_core) - 1;
		uint32_t thread_mask = (1 << bits_for_thread) - 1;
	
		topology_ids[0] = initial_apic_id >> bits_below_package;
		topology_types[0] = TOPOLOGY_TYPE_PACKAGE;

		topology_ids[1] = (initial_apic_id >> bits_for_thread) & core_mask;
		topology_types[1] = TOPOLOGY_TYPE_CORE;

		topology_ids[2] = initial_apic_id & thread_mask;
		topology_types[2] = TOPOLOGY_TYPE_THREAD;
	} else if (is_amd && has_package_bits && cpuid_leaf_0x80000008_available()) {
		__get_cpuid(0x80000008, &eax, &ebx, &ecx, &edx);
		size_t apic_id_size = (ecx >> 12) & 0xf;
		size_t bits_for_core = apic_id_size ? apic_id_size : 32 - __builtin_clz(ecx & 0xff);
		size_t bits_for_thread = bits_below_package - bits_for_core;
		uint32_t core_mask = (1 << bits_for_core) - 1;
		uint32_t thread_mask = (1 << bits_for_thread) - 1;
	
		topology_ids[0] = initial_apic_id >> bits_below_package;
		topology_types[0] = TOPOLOGY_TYPE_PACKAGE;

		topology_ids[1] = (initial_apic_id >> bits_for_thread) & core_mask;
		topology_types[1] = TOPOLOGY_TYPE_CORE;

		topology_ids[2] = initial_apic_id & thread_mask;
		topology_types[2] = TOPOLOGY_TYPE_THREAD;
	} else if (has_package_bits) {
		topology_ids[0] = initial_apic_id >> bits_below_package;
		topology_types[0] = TOPOLOGY_TYPE_PACKAGE;

		topology_ids[1] = initial_apic_id & ((1 << bits_below_package) - 1);
		topology_types[1] = TOPOLOGY_TYPE_CORE;

		topology_ids[2] = 0;
		topology_types[2] = TOPOLOGY_TYPE_THREAD;
	} else {
		*topology_ids = initial_apic_id;
		*topology_types = TOPOLOGY_TYPE_THREAD;
	}
}

void arch_cpu_init() {
	// syscall instruction
	unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
	__get_cpuid(0x80000001, &eax, &ebx, &ecx, &edx);

	__assert(edx & CPUID_LEAF_0x80000001_EDX_SYSCALL);
	uint64_t efer = rdmsr(MSR_EFER);
	efer |= EFER_SYSCALLENABLE;
	wrmsr(MSR_EFER, efer);

	uint64_t star = 0;
	star |= (uint64_t)0x13 << 48;
	star |= (uint64_t)0x08 << 32;

	wrmsr(MSR_STAR, star);
	wrmsr(MSR_LSTAR, (uint64_t)arch_syscall_entry);
	wrmsr(MSR_CSTAR, 0); // no compatibility mode syscall handler
	wrmsr(MSR_FMASK, 0x200); // disable interrupts on syscall

	// enable SSE
	asm volatile(
		"mov %%cr0, %%rax;"
		"and $0xFFFB, %%ax;"
		"or  $2, %%eax;"
		"mov %%rax, %%cr0;"
		"mov %%cr4, %%rax;"
		"or  $0b11000000000, %%rax;"
		"mov %%rax, %%cr4;"
		: : : "rax");

	// set NE in cr0 and reset x87 fpu
	asm volatile(
		"fninit;"
		"mov %%cr0, %%rax;"
		"or $0b100000, %%rax;"
		"mov %%rax, %%cr0;"
		: : : "rax"
	);

	x86_64_mmu_enable_global_pages();

	// register some exception handlers that give out signals
	interrupt_register(0, sigfpeisr, NULL, IPL_IGNORE);
	interrupt_register(1, dbisr, NULL, IPL_IGNORE);
	interrupt_register(3, dbisr, NULL, IPL_IGNORE);
	interrupt_register(4, sigfpeisr, NULL, IPL_IGNORE);
	interrupt_register(5, sigfpeisr, NULL, IPL_IGNORE);
	interrupt_register(6, illisr, NULL, IPL_IGNORE);
	interrupt_register(16, x87isr, NULL, IPL_IGNORE);
	interrupt_register(19, simdisr, NULL, IPL_IGNORE);

	// and the ldt reload one
	interrupt_register(0xfc, (void *)reload_ldt, NULL, IPL_MAX);

	// get vendor string for cpu and max cpuid eax
	__get_cpuid(0, &eax, &ebx, &ecx, &edx);
	int *vendor_ptr = (int *)current_cpu()->vendor;
	*vendor_ptr++ = ebx;
	*vendor_ptr++ = edx;
	*vendor_ptr++ = ecx;
	current_cpu()->vendor[12] = '\0';
	current_cpu()->cpuid_max = eax;

	size_t topology_depth = get_topology_depth();
	long topology_ids[topology_depth];
	long topology_types[topology_depth];
	get_topology(topology_ids, topology_types);

	char str_buf[128];
	size_t done = 0;

	done += snprintf(str_buf, 128, "cpu%d: %s@", current_cpu()->internal_id, current_cpu()->vendor);
	for (int i = 0; i < topology_depth; ++i)
		done += snprintf(str_buf + done, 128 - done, "%d%c", topology_ids[i], i == topology_depth - 1 ? 0 : ':');
	str_buf[done] = '\n';
	printf("%s\n", str_buf);

	// insert it into the topology tree
	topology_node_t *topology_nodes[topology_depth];
	for (int i = 0; i < topology_depth; ++i) {
		int id = TOPOLOGY_MAKE_ID(topology_types[i], topology_ids[i]);
		topology_node_t *parent = i == 0 ? topology_get_root() : topology_nodes[i - 1];

		topology_nodes[i] = topology_find_child_by_id(parent, id);
		if (topology_nodes[i] == NULL) {
			topology_nodes[i] = topology_create_node();
			__assert(topology_nodes[i]);

			topology_insert(topology_nodes[i], i == 0 ? topology_get_root() : topology_nodes[i - 1],
				       	TOPOLOGY_MAKE_ID(topology_types[i], topology_ids[i]), (i == topology_depth - 1) ? current_cpu() : NULL);
		}
	}

	current_cpu()->topology_node = topology_nodes[topology_depth - 1];

	// allocate ists
	uint64_t *ist1 = vmm_map(NULL, PAGE_SIZE * 4, VMM_FLAGS_ALLOCATE, ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC, NULL);
	__assert(ist1);

	// the cpu automatically aligns to 16 bytes when pushing the interrupt frame, so save ourselves the trouble:
	ist1 += PAGE_SIZE * 4 / sizeof(uint64_t) - 2;
	*ist1 = (uint64_t)current_cpu();
	current_cpu()->ist.ist1 = (uint64_t)ist1;
}

INIT_ROUTINE_DEFINE(cpu, INIT_ROUTINE_FLAGS_NONE, arch_cpu_init, acpi_early);

static void infinite_loop(void) {
	asm volatile("cli");
	for (;;) asm volatile("");
}

static void arch_early(void) {
	arch_gdt_reload();
	arch_idt_setup();
	arch_idt_reload();
	interrupt_register(0xfd, (void *)infinite_loop, NULL, IPL_IGNORE);
	dpc_init();
	arch_extracontext_detect();
}

INIT_ROUTINE_DEFINE(arch_early, INIT_ROUTINE_FLAGS_NONE, arch_early)
