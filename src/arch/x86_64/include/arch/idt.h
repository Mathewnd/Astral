#ifndef _IDT_H_INCLUDE
#define _IDT_H_INCLUDE

#include <stdint.h>

#define FLAGS_PRESENT (1 << 15)
#define FLAGS_DPL3    (1 << 13)
#define FLAGS_TYPE_INTERRUPT (0xE << 8)
#define FLAGS_TYPE_TRAP (0xF << 8)

#define VECTOR_DIV 0
#define VECTOR_DEBUG 1
#define VECTOR_NMI 2
#define VECTOR_BREAKPOINT 3
#define VECTOR_OVERFLOW 4
#define VECTOR_BOUNDRANGE 5
#define VECTOR_INVALIDOPCODE 6
#define VECTOR_DEVUNAVAILABLE 7
#define VECTOR_DOUBLEFAULT 8
#define VECTOR_COPROCESSOR 9
#define VECTOR_INVALIDTSS 10
#define VECTOR_SEGMENTNOTPRESENT 11
#define VECTOR_STACK 12
#define VECTOR_GPF 13
#define VECTOR_PF 14

#define VECTOR_SIMD 0x13

#define VECTOR_PANIC 0x20
#define VECTOR_LAPICNMI 0x21
#define VECTOR_MMUINVAL 0x22
#define VECTOR_PS2MOUSE 0x3F
#define VECTOR_PS2KBD   0x40
#define VECTOR_TIMER 0x80

#define TIMER_IST 1

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
void idt_init();

#endif
