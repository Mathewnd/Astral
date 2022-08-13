#include <arch/smp.h>
#include <limine.h>
#include <stdio.h>
#include <arch/panic.h>
#include <arch/spinlock.h>
#include <stddef.h>
#include <arch/cls.h>
#include <arch/gdt.h>
#include <arch/idt.h>
#include <arch/mmu.h>
#include <arch/apic.h>
#include <kernel/pmm.h>
#include <kernel/semaphore.h>

static cls_t* apcls;
static semaphore_t* sem;

static size_t cpucount = 0;

static void apstartup(struct limine_smp_info *info){
	
	arch_setcls(&apcls[info->processor_id]);
	
	gdt_init();

	idt_init();	

	arch_mmu_apinit();
	
	apic_lapicinit();
	
	timer_init();

	printf("CPU %lu ready!\n", info->lapic_id);

	sem_signal(sem);
	
	for(;;) asm("hlt");

}

size_t arch_smp_cpucount(){
	return cpucount;
}

void arch_smp_sendipi(int processor, int vector, int mode){
	apic_sendipi(processor, vector, mode, 0, 1);
}

static volatile struct limine_smp_request smpreq = {
	.id = LIMINE_SMP_REQUEST,
	.revision = 0
};

void smp_init(){
	
	struct limine_smp_response *r = smpreq.response;

	if(!r)
		_panic("Limine did not bring up the other CPUs!", 0);

	cpucount = r->cpu_count;
	struct limine_smp_info** cpus = r->cpus;
	
	apcls = pmm_hhdmalloc(sizeof(cls_t)*cpucount/PAGE_SIZE+1);

	memset(apcls, 0, sizeof(cls_t)*cpucount/PAGE_SIZE+1);

	printf("System has %lu CPUs\n", cpucount);
	
	sem = sem_init(-cpucount + 2, 0);

	for(size_t i = 0; i < cpucount; ++i){
		if(cpus[i]->lapic_id == r->bsp_lapic_id) continue;
		printf("Dispatching CPU %lu\n", i);	
		asm volatile(".intel_syntax noprefix;"
			"lock xchg rax, [rbx];"
			".att_syntax prefix;"
			:
			: "b"(&cpus[i]->goto_address), "a"(apstartup));
	}

	while(!sem_tryacquire(sem)) asm volatile("pause");

	printf("All processors ready!\n");
}
