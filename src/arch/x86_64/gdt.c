#include <arch/cls.h>
#include <arch/gdt.h>
#include <arch/ist.h>

static void setseg(segdesc __seg_gs *seg, uint32_t limit, uint32_t offset, uint8_t flags, uint8_t access){
	
	seg->limit = limit & 0xFFFF;
	seg->offset1 = offset & 0xFFFF;
	seg->offset2 = (offset >> 16) & 0xFF;
	seg->access = access;
	seg->flags = flags << 4;
	seg->flags |= (limit >> 16) & 0xFF;
	seg->offset3 = (offset >> 24) & 0xFF;

}

static void setsys64(sys64 __seg_gs *e, uint32_t limit, uint64_t offset, uint8_t flags, uint8_t access){

	e->limit = limit & 0xFFFF;
	e->offset1 = offset & 0xFFFF;
	e->offset2 = (offset >> 16) & 0xFF;
	e->access = access;
	e->flags = flags << 4;
	e->flags |= (limit >> 16) & 0xFF;
	e->offset3 = (offset >> 24) & 0xFF;
	e->offset4 = (offset >> 32) & 0xFFFFFFFF;
	e->reserved = 0;
}

gdtr_t gdtr;	


static void reloadgdtr(){
	
gdtr.offset = (uint64_t)&cls->gdt + arch_getcls();
gdtr.size   = sizeof(gdt_t) - 1;
	asm("lgdt (%%rax)" : : "a"(&gdtr) : "memory");
}

void gdt_bspinit(){

	gdt_t __seg_gs *gdt = &cls->gdt;
	
	setseg(&gdt->null, 0, 0, 0, 0);

	setseg(&gdt->kcode16, 0xFFFFF, 0, FLAGS_PAGE_GRANULARITY, ACCESS_PRESENT | ACCESS_SEGMENT | ACCESS_EXEC | ACCESS_CODE_READ);
	
	setseg(&gdt->kdata16, 0xFFFFF, 0, FLAGS_PAGE_GRANULARITY, ACCESS_PRESENT | ACCESS_SEGMENT | ACCESS_DATA_WRITE);
	
	setseg(&gdt->kcode32, 0xFFFFF, 0, FLAGS_PAGE_GRANULARITY | FLAGS_PMODE, ACCESS_PRESENT | ACCESS_SEGMENT | ACCESS_EXEC | ACCESS_CODE_READ);
	
	setseg(&gdt->kdata32, 0xFFFFF, 0, FLAGS_PAGE_GRANULARITY | FLAGS_PMODE, ACCESS_PRESENT | ACCESS_SEGMENT | ACCESS_DATA_WRITE);
	
	setseg(&gdt->kcode64, 0xFFFFF, 0, FLAGS_PAGE_GRANULARITY | FLAGS_LONG, ACCESS_PRESENT | ACCESS_SEGMENT | ACCESS_EXEC | ACCESS_CODE_READ);
	
	setseg(&gdt->kdata64, 0xFFFFF, 0, FLAGS_PAGE_GRANULARITY | FLAGS_LONG, ACCESS_PRESENT | ACCESS_SEGMENT | ACCESS_DATA_WRITE);
	

	setseg(&gdt->ucode64, 0xFFFFF, 0, FLAGS_PAGE_GRANULARITY | FLAGS_LONG, ACCESS_PRESENT | ACCESS_SEGMENT | ACCESS_EXEC | ACCESS_CODE_READ | ACCESS_DPL3);
	
	setseg(&gdt->udata64, 0xFFFFF, 0, FLAGS_PAGE_GRANULARITY | FLAGS_LONG, ACCESS_PRESENT | ACCESS_SEGMENT | ACCESS_DATA_WRITE | ACCESS_DPL3);
	
	// TODO get this working right

	//setsys64(&gdt->ist, sizeof(ist_t), (uint64_t)(arch_getcls()), FLAGS_LONG, SYSTEM_TYPE_IST | ACCESS_PRESENT);

	reloadgdtr();


	
}
