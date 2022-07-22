#ifndef _IDT_H_INCLUDE
#define _IDT_H_INCLUDE

#include <stdint.h>

#define FLAGS_PRESENT (1 << 15)
#define FLAGS_DPL3    (1 << 13)
#define FLAGS_TYPE_INTERRUPT (0xE << 8)
#define FLAGS_TYPE_TRAP (0xF << 8)



typedef struct{
	uint16_t size;
	uint64_t offset;
} __attribute__((packed)) idtr_t;

typedef struct{
	uint16_t offset1;
	uint16_t segment;
	uint16_t  flags;
	uint16_t  offset2;
	uint32_t  offset3;
	uint32_t  reserved;

} __attribute__((packed)) idtentry_t;

void idt_bspinit();

#endif
