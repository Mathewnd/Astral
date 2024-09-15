#include <kernel/dpc.h>
#include <arch/cpu.h>
#include <kernel/interrupt.h>
#include <logging.h>


static void remove(dpc_t *dpc) {
	if (dpc->prev)
		dpc->prev->next = dpc->next;
	else 
		current_cpu()->dpcqueue = dpc->next;

	if (dpc->next)
		dpc->next->prev = dpc->prev;
}

static void insert(dpc_t *dpc) {
	dpc->prev = NULL;
	dpc->next = current_cpu()->dpcqueue;

	if (dpc->next)
		dpc->next->prev = dpc;

	current_cpu()->dpcqueue = dpc;
}

static void isrfn(isr_t *self, context_t *context) {
	while (current_cpu()->dpcqueue) {
		dpc_t *dpc = current_cpu()->dpcqueue;
		remove(dpc);
		dpcarg_t arg = dpc->arg;

		__assert(dpc->enqueued);
		dpc->enqueued = false;

		interrupt_set(true);
		dpc->fn(context, arg);
		interrupt_set(false);
	}
}

void dpc_enqueue(dpc_t *dpc, dpcfn_t fn, dpcarg_t arg) {
	bool entrystate = interrupt_set(false);

	if (dpc->enqueued)
		goto cleanup;

	dpc->fn = fn;
	dpc->arg = arg;
	dpc->enqueued = true;
	insert(dpc);
	interrupt_raise(current_cpu()->dpcisr);

	cleanup:
	interrupt_set(entrystate);
}

void dpc_dequeue(dpc_t *dpc) {
	bool entrystate = interrupt_set(false);

	if (dpc->enqueued == false)
		goto cleanup;

	dpc->enqueued = false;
	remove(dpc);

	cleanup:
	interrupt_set(entrystate);
}

void dpc_init() {
	current_cpu()->dpcisr = interrupt_allocate(isrfn, NULL, IPL_DPC);
	__assert(current_cpu()->dpcisr);
}
