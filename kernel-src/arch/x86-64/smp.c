#include <limine.h>
#include <logging.h>
#include <kernel/pmm.h>
#include <arch/cpu.h>
#include <arch/gdt.h>
#include <arch/idt.h>
#include <kernel/cmdline.h>
#include <arch/smp.h>
#include <kernel/alloc.h>

static volatile struct limine_smp_request smprequest = {
	.id = LIMINE_SMP_REQUEST
};

// set to 1 because of the bsp
size_t arch_smp_cpusawake = 1;

// for panic. if the nosmp argument is given to the kernel, this is what the APs will jump to
static void cpuwakeuphalt(struct limine_smp_info *info) {
	asm("cli");
	for (;;) CPU_HALT();
}

static void cpuwakeup(struct limine_smp_info *info) {
	cpu_set((cpu_t *)info->extra_argument);

	arch_gdt_reload();
	arch_idt_reload();
	interrupt_register(0xfd, (void *)cpuwakeuphalt, NULL, IPL_IGNORE);
	dpc_init();
	arch_mmu_apswitch();
	vmm_apinit();

	cpu_initstate();
	arch_apic_timerinit();

	__atomic_add_fetch(&arch_smp_cpusawake, 1, __ATOMIC_SEQ_CST);

	sched_apentry();
}

void arch_smp_sendipi(cpu_t *targcpu, isr_t *isr, int target, bool nmi) {
	arch_apic_sendipi(targcpu ? targcpu->id : 0, INTERRUPT_IDTOVECTOR(isr->id), target, nmi ? APIC_MODE_NMI : 0, 0);
}

void arch_smp_haltallothers() {
	arch_smp_sendipi(NULL, &current_cpu()->isr[0xfd], ARCH_SMP_IPI_OTHERCPUS, true);
}

cpu_t **smp_cpus;

void arch_smp_wakeup() {
	interrupt_register(0xfd, (void *)cpuwakeuphalt, NULL, IPL_IGNORE);
	struct limine_smp_response *response = smprequest.response;
	if (response == NULL) {
		// TODO try to detect the other processors manually
		printf("smp: limine smp request response not present\n");
		return;
	}

	printf("smp: %d processor%s\n", response->cpu_count, response->cpu_count > 1 ? "s" : "");

	// use physical pages so the other cpus have it on the hhdm
	size_t cpu_size = ROUND_UP(sizeof(cpu_t) * response->cpu_count, PAGE_SIZE);
	cpu_t *cpus = pmm_alloc(cpu_size / PAGE_SIZE, PMM_SECTION_DEFAULT);
	__assert(cpus);
	cpus = MAKE_HHDM(cpus);
	memset(cpus, 0, cpu_size);

	smp_cpus = alloc(response->cpu_count * sizeof(cpu_t *));
	__assert(smp_cpus);

	void (*wakeupfn)(struct limine_smp_info *) = cmdline_get("nosmp") ? cpuwakeuphalt : cpuwakeup;

	// make the other processors jump to cpuwakeup()
	for (int i = 0; i < response->cpu_count; ++i) {
		// skip the bootstrap processor
		if (response->cpus[i]->lapic_id == response->bsp_lapic_id) {
			smp_cpus[i] = get_bsp();
			continue;
		}

		smp_cpus[i] = &cpus[i];
		response->cpus[i]->extra_argument = (uint64_t)&cpus[i];

		__atomic_store_n(&response->cpus[i]->goto_address, wakeupfn, __ATOMIC_SEQ_CST);
	}

	// wait for other cpus to boot up
	if (wakeupfn == cpuwakeup)
		while (__atomic_load_n(&arch_smp_cpusawake, __ATOMIC_SEQ_CST) != response->cpu_count) asm("pause");

	printf("smp: awoke other processors\n");
}
