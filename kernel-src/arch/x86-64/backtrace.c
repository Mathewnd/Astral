#include <arch/backtrace.h>
#include <stdint.h>

static bool valid_rip(void *rip) {
	if ((uint64_t)rip < (uint64_t)0xffffffff80000000)
		return false;

	return true;
}

// TODO 5-level paging support?
static bool valid_frame(void *frame, void *old_frame) {
	if ((uint64_t)frame < (uint64_t)0xffff800000000000)
		return false;

	if ((uint64_t)frame >= (uint64_t)0xffffffff80000000)
		return false;

	if ((uint64_t)old_frame >= (uint64_t)frame)
		return false;

	return true;
}

void arch_get_backtrace(context_t *cpu_context, backtrace_fn_t fn, void *context) {
	if (!valid_rip((void *)cpu_context->rip))
		return;

	if (!fn(context, (void *)cpu_context->rip))
		return;

	size_t loops = 0;
	uint64_t *rbp = (uint64_t *)cpu_context->rbp;
	uint64_t *old_frame = NULL;
	for (;;) {
		if (!valid_frame(rbp, old_frame))
			return;

		void *rip = (void *)*(rbp + 1);
		if (!valid_rip(rip))
			return;

		if (!fn(context, rip))
			return;

		old_frame = rbp;
		rbp = (uint64_t *)*rbp;

		if (++loops == 256)
			return;
	}
}

