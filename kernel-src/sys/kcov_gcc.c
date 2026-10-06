#include <kernel/kcov.h>
#include <arch/cpu.h>

#ifdef KCOV_ENABLED

KCOV_DISABLED void __sanitizer_cov_trace_pc(void) {
	if (current_thread() == NULL || current_thread()->kcov == NULL ||
	    current_thread()->kcov->mode != KCOV_MODE_PC)
		return;

	if (current_irq_depth() > 0)
		return;

	uint64_t pc = (uint64_t)__builtin_return_address(0);
	kcov_publish_pc(pc);
}

KCOV_DISABLED static void publish_cmp(uint64_t pc, uint64_t v1, uint64_t v2, uint64_t flags) {
	if (current_thread() == NULL || current_thread()->kcov == NULL ||
	    current_thread()->kcov->mode != KCOV_MODE_CMP)
		return;

	if (current_irq_depth() > 0)
		return;

	kcov_publish_cmp(pc, v1, v2, flags);
}

KCOV_DISABLED void __sanitizer_cov_trace_const_cmp1(uint8_t a1, uint8_t a2) {
	uint64_t pc = (uint64_t)__builtin_return_address(0);
	publish_cmp(pc, a1, a2, KCOV_CMP_FLAGS_CONST | KCOV_CMP_FLAGS_8BIT);
}

KCOV_DISABLED void __sanitizer_cov_trace_const_cmp2(uint16_t a1, uint16_t a2) {
	uint64_t pc = (uint64_t)__builtin_return_address(0);
	publish_cmp(pc, a1, a2, KCOV_CMP_FLAGS_CONST | KCOV_CMP_FLAGS_16BIT);
}

KCOV_DISABLED void __sanitizer_cov_trace_const_cmp4(uint32_t a1, uint32_t a2) {
	uint64_t pc = (uint64_t)__builtin_return_address(0);
	publish_cmp(pc, a1, a2, KCOV_CMP_FLAGS_CONST | KCOV_CMP_FLAGS_32BIT);
}

KCOV_DISABLED void __sanitizer_cov_trace_const_cmp8(uint64_t a1, uint64_t a2) {
	uint64_t pc = (uint64_t)__builtin_return_address(0);
	publish_cmp(pc, a1, a2, KCOV_CMP_FLAGS_CONST | KCOV_CMP_FLAGS_64BIT);
}

KCOV_DISABLED void __sanitizer_cov_trace_cmp1(uint8_t a1, uint8_t a2) {
	uint64_t pc = (uint64_t)__builtin_return_address(0);
	publish_cmp(pc, a1, a2, KCOV_CMP_FLAGS_8BIT);
}

KCOV_DISABLED void __sanitizer_cov_trace_cmp2(uint16_t a1, uint16_t a2) {
	uint64_t pc = (uint64_t)__builtin_return_address(0);
	publish_cmp(pc, a1, a2, KCOV_CMP_FLAGS_16BIT);
}

KCOV_DISABLED void __sanitizer_cov_trace_cmp4(uint32_t a1, uint32_t a2) {
	uint64_t pc = (uint64_t)__builtin_return_address(0);
	publish_cmp(pc, a1, a2, KCOV_CMP_FLAGS_32BIT);

}

KCOV_DISABLED void __sanitizer_cov_trace_cmp8(uint64_t a1, uint64_t a2) {
	uint64_t pc = (uint64_t)__builtin_return_address(0);
	publish_cmp(pc, a1, a2, KCOV_CMP_FLAGS_64BIT);
}

KCOV_DISABLED void __sanitizer_cov_trace_switch(uint64_t v, uint64_t *cases) {
	if (current_thread() == NULL || current_thread()->kcov == NULL ||
	    current_thread()->kcov->mode != KCOV_MODE_CMP)
		return;

	if (current_irq_depth() > 0)
		return;

	uint64_t pc = (uint64_t)__builtin_return_address(0);
	uint64_t flags = KCOV_CMP_FLAGS_CONST;
	switch (cases[1]) {
		case 8:
			flags |= KCOV_CMP_FLAGS_8BIT;
			break;
		case 16:
			flags |= KCOV_CMP_FLAGS_16BIT;
			break;
		case 32:
			flags |= KCOV_CMP_FLAGS_32BIT;
			break;
		case 64:
			flags |= KCOV_CMP_FLAGS_64BIT;
			break;
		default:
			return;
	}

	for (int i = 0; i < cases[0]; ++i)
		kcov_publish_cmp(pc, cases[2 + i], v, flags);
}

#endif
