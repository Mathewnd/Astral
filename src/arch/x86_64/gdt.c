#include <arch/cls.h>
#include <arch/gdt.h>
#include <arch/ist.h>

static void setseg(segdesc __seg_gs *seg, uint32_t limit, uint32_t offset, uint8_t flags, uint8_t access){

}

static void setsys64(sys64 __seg_gs *e, uint32_t limit, uint64_t offset, uint8_t flags, uint8_t access){

}

static void reloadgdtr(){
	gdtr_t gdtr;	
	gdtr.offset = (uint64_t)&cls->gdt + arch_getcls();
	gdtr.size   = sizeof(gdt_t) - 1;

	asm("lgdt (%%rax)" : : "a"(&gdtr) : "memory");
}

void gdt_bspinit(){

	gdt_t __seg_gs *gdt = &cls->gdt;
	
	setseg(&gdt->null, 0, 0, 0, 0);

	setseg(&gdt->kcode16, 0xFFFFF, 0, FLAGS_PAGE_GRANULARITY, ACCESS_PRESENT | ACCESS_SEGMENT | ACCESS_EXEC | ACCESS_CODE_READ);
	
	setseg(&gdt->kdata16, 0xFFFFF, 0, FLAGS_PAGE_GRANULARITY, ACCESS_PRESENT | ACCESS_SEGMENT | ACCESS_DATA_WRITE);
	
	setseg(&gdt->kcode32, 0xFFFFF, 0, FLAGS_PAGE_GRANULARITY | FLAGS_PMODE, ACCESS_PRESENT | ACCESS_SEGMENT | ACCESS_CODE_READ);
	
	setseg(&gdt->kdata32, 0xFFFFF, 0, FLAGS_PAGE_GRANULARITY | FLAGS_PMODE, ACCESS_PRESENT | ACCESS_SEGMENT | ACCESS_DATA_WRITE);
	
	setseg(&gdt->kcode64, 0, 0, FLAGS_PAGE_GRANULARITY | FLAGS_LONG, ACCESS_PRESENT | ACCESS_SEGMENT | ACCESS_EXEC | ACCESS_CODE_READ);
	
	setseg(&gdt->kdata64, 0, 0, FLAGS_PAGE_GRANULARITY | FLAGS_LONG, ACCESS_PRESENT | ACCESS_SEGMENT | ACCESS_DATA_WRITE);
	

	setseg(&gdt->kcode64, 0, 0, FLAGS_PAGE_GRANULARITY | FLAGS_LONG, ACCESS_PRESENT | ACCESS_SEGMENT | ACCESS_EXEC | ACCESS_CODE_READ | ACCESS_DPL3);
	
	setseg(&gdt->kdata64, 0, 0, FLAGS_PAGE_GRANULARITY | FLAGS_LONG, ACCESS_PRESENT | ACCESS_SEGMENT | ACCESS_DATA_WRITE | ACCESS_DPL3);
	
	// TODO get this working right

	//setsys64(&gdt->ist, sizeof(ist_t), (uint64_t)(arch_getcls()), FLAGS_LONG, SYSTEM_TYPE_IST | ACCESS_PRESENT);

	reloadgdtr();


	
}
