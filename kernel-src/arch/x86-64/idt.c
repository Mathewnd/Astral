#include <arch/idt.h>
#include <stdint.h>
#include <stddef.h>
#include <logging.h>
#include <panic.h>
#include <kernel/interrupt.h>
#include <string.h>
#include <arch/cpu.h>

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

void asm_nmi_handler();
extern const uint64_t isr_table[256];

void arch_idt_setup() {
	for (int i = 0; i < 256; ++i) {
		uint64_t addr = i == 2 ? (uint64_t)asm_nmi_handler : isr_table[i];
		idt[i].offset = addr & 0xffff;
		idt[i].segment = 0x8;
		idt[i].ist = i == 2 ? 1 : 0;
		idt[i].flags = 0x8e;
		idt[i].offset2 = (addr >> 16) & 0xffff;
		idt[i].offset3 = (addr >> 32) & 0xffffffff;
	}
}

static char *exceptions[] = {
	"Division by 0",
	"Debug",
	"NMI",
	"Breakpoint",
	"Overflow",
	"Bound Range Exceeded",
	"Invalid Opcode",
	"Device Not Available",
	"Double Fault",
	"Coprocessor Segment Overrun",
	"Invalid TSS",
	"Segment Not Present",
	"Stack-Segment Fault",
	"General Protection Fault",
	"Page Fault",
	"Unknown",
	"x87 Floating-Point Exception",
	"Alignment Check",
	"Machine Check",
	"SIMD Exception"
};

static void exceptisr(isr_t *isr, context_t *ctx) {
	char a[64];
	if ((isr->id & 0xff) < 19)
		sprintf(a, "CPU exception (%s)\n", exceptions[isr->id]);
	else
		strcpy(a, "Unkown CPU exception");
	_panic(a, ctx);
}

void arch_idt_reload() {
	asm volatile("lidt (%%rax)" : : "a"(&idtr));

	// make sure to reserve the first 32 isrs for exceptions with a dummy panic isr
	for (int i = 0; i < 32; ++i)
		interrupt_register(i, exceptisr, NULL, IPL_IGNORE);

	current_cpu()->ipl = IPL_NORMAL;
	interrupt_set(true);
}

void arch_interrupt_disable() {
	asm("cli");
}

void arch_interrupt_enable() {
	asm("sti");
}
