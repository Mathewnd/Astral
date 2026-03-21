#include <arch/cpu.h>
#include <arch/ist.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <printf.h>

static uint64_t template[9] = {
	0, // NULL 0x0
	0x00af9b000000ffff, // code64 0x8
	0x00af93000000ffff, // data64 0x10
	0x00eff3000000ffff, // udata64 0x18
	0x00affb000000ffff, // ucode64 0x20
	0x0020890000000000, // low ist 0x28
	0x0000000000000000, // high ist
	0x0000000000000000, // LDT low
	0x0000000000000000  // LDT high
};

// should be called with preemption DISABLED
void arch_gdt_set_ldt(void *base, size_t limit) {
	if (base == NULL) {
		current_cpu()->gdt[7] = 0;
		current_cpu()->gdt[8] = 0;
		asm volatile("lldt %%ax" : : "a"(0));
		return;
	}

	uint64_t base_low = (uint64_t)base & 0xffffffff;
	current_cpu()->gdt[7] = 0x82000000ffff; // present, ldt, limit 64k
	current_cpu()->gdt[7] |= (base_low & 0xffff) << 16;
	current_cpu()->gdt[7] |= ((base_low >> 16) & 0xff) << 32;
	current_cpu()->gdt[7] |= ((base_low >> 24) & 0xff) << 56;
	current_cpu()->gdt[8] = ((uint64_t)base >> 32) & 0xffffffff;
	asm volatile("lldt %%ax" : : "a"(0x38));
}

typedef struct {
	uint16_t size;
	uint64_t address;
} __attribute__((packed)) gdtr_t;

void arch_gdt_reload() {
	memcpy(current_cpu()->gdt, template, sizeof(template));
	uintptr_t istaddr = (uintptr_t)&current_cpu()->ist;

	// add ist to gdt
	current_cpu()->gdt[5] |= ((istaddr & 0xff000000) << 32) | ((istaddr & 0xff0000) << 16) | ((istaddr & 0xffff) << 16) | sizeof(ist_t);
	current_cpu()->gdt[6] = (istaddr >> 32) & 0xffffffff;

	// reload gdt and ist

	gdtr_t gdtr = {
		.size = sizeof(template) - 1,
		.address = (uintptr_t)&current_cpu()->gdt
	};

	asm volatile("lgdt (%%rax)" : : "a"(&gdtr) : "memory");
	asm volatile("ltr %%ax" : : "a"(0x28));
	asm volatile(
		"swapgs;"
		"mov $0, %%ax;"
		"mov %%ax, %%gs;"
		"mov %%ax, %%fs;"
		"swapgs;"
		"mov $0x10, %%ax;"
		"mov %%ax, %%ds;"
		"mov %%ax, %%es;"
		"mov %%ax, %%ss;"
		"pushq $0x8;"
		"pushq $.reload;"
		"retfq;"
		".reload:"
		: : : "ax");
}
