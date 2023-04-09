#include <arch/idt.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
	uint16_t size;
	uint64_t offset;
} __attribute__((packed)) idtr_t;

typedef struct {
	uint16_t offset;
	uint16_t segment;
	uint8_t ist;
	uint8_t flags;
	uint16_t offset2;
	uint32_t offset3;
	uint32_t reserved;
} __attribute__((packed)) idtentry_t;

static idtentry_t idt[256];

static idtr_t idtr = {
	.size = sizeof(idt) - 1,
	.offset = (uint64_t)idt
};

extern const uint64_t isr_table[256];

void arch_idt_setup() {
	for (int i = 0; i < 256; ++i) {
		idt[i].offset = isr_table[i] & 0xffff;
		idt[i].segment = 0x8;
		idt[i].ist = 0;
		idt[i].flags = 0x8e;
		idt[i].offset2 = (isr_table[i] >> 16) & 0xffff;
		idt[i].offset3 = (isr_table[i] >> 32) & 0xffffffff;
	}
}

void arch_idt_reload() {
	asm volatile("lidt (%%rax)" : : "a"(&idtr));
}
