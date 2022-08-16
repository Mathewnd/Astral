#include <arch/cls.h>
#include <arch/gdt.h>
#include <arch/ist.h>

static void setseg(segdesc *seg, uint32_t limit, uint32_t offset, uint8_t flags, uint8_t access){
	
	seg->limit = limit & 0xFFFF;
	seg->offset1 = offset & 0xFFFF;
	seg->offset2 = (offset >> 16) & 0xFF;
	seg->access = access;
	seg->flags = flags << 4;
	seg->flags |= (limit >> 16) & 0xFF;
	seg->offset3 = (offset >> 24) & 0xFF;

}

static void setsys64(sys64 *e, uint32_t limit, uint64_t offset, uint8_t flags, uint8_t access){

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
	
gdtr.offset = &arch_getcls()->gdt;
gdtr.size   = sizeof(gdt_t) - 1;
	asm volatile("lgdt (%%rax)" : : "a"(&gdtr) : "memory");
	asm volatile("ltr %%ax" : : "a"(0x48));
	asm volatile("swapgs;mov $0, %%ax;mov %%ax, %%gs;mov %%ax, %%fs;swapgs;" : : : "ax");

}

void gdt_init(){
	
	gdt_t* gdt = &arch_getcls()->gdt;
	
	setseg(&gdt->null, 0, 0, 0, 0);

	setseg(&gdt->kcode16, 0xFFFFF, 0, FLAGS_PAGE_GRANULARITY, ACCESS_PRESENT | ACCESS_SEGMENT | ACCESS_EXEC | ACCESS_CODE_READ);
	
	setseg(&gdt->kdata16, 0xFFFFF, 0, FLAGS_PAGE_GRANULARITY, ACCESS_PRESENT | ACCESS_SEGMENT | ACCESS_DATA_WRITE);
	
	setseg(&gdt->kcode32, 0xFFFFF, 0, FLAGS_PAGE_GRANULARITY | FLAGS_PMODE, ACCESS_PRESENT | ACCESS_SEGMENT | ACCESS_EXEC | ACCESS_CODE_READ);
	
	setseg(&gdt->kdata32, 0xFFFFF, 0, FLAGS_PAGE_GRANULARITY | FLAGS_PMODE, ACCESS_PRESENT | ACCESS_SEGMENT | ACCESS_DATA_WRITE);
	
	setseg(&gdt->kcode64, 0xFFFFF, 0, FLAGS_PAGE_GRANULARITY | FLAGS_LONG, ACCESS_PRESENT | ACCESS_SEGMENT | ACCESS_EXEC | ACCESS_CODE_READ);
	
	setseg(&gdt->kdata64, 0xFFFFF, 0, FLAGS_PAGE_GRANULARITY | FLAGS_LONG, ACCESS_PRESENT | ACCESS_SEGMENT | ACCESS_DATA_WRITE);
	

	setseg(&gdt->ucode64, 0xFFFFF, 0, FLAGS_PAGE_GRANULARITY | FLAGS_LONG, ACCESS_PRESENT | ACCESS_SEGMENT | ACCESS_EXEC | ACCESS_CODE_READ | ACCESS_DPL3);
	
	setseg(&gdt->udata64, 0xFFFFF, 0, FLAGS_PAGE_GRANULARITY | FLAGS_LONG, ACCESS_PRESENT | ACCESS_SEGMENT | ACCESS_DATA_WRITE | ACCESS_DPL3);

	setsys64(&gdt->ist, sizeof(ist_t), (uint64_t)(&arch_getcls()->ist), FLAGS_LONG, SYSTEM_TYPE_IST | ACCESS_PRESENT);

	reloadgdtr();


	
}
