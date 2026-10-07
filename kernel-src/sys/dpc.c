#include <kernel/dpc.h>
#include <arch/cpu.h>
#include <kernel/interrupt.h>
#include <logging.h>


static void isrfn(isr_t *self, context_t *context) {
	list_node_t *node;
	while ((node = list_pop_front(&current_cpu()->dpcqueue)) != NULL) {
		dpc_t *dpc = (dpc_t *)node;
		dpcarg_t arg = dpc->arg;

		__assert(dpc->enqueued);
		dpc->enqueued = false;

		interrupt_set(true);
		dpc->fn(context, arg);
		interrupt_set(false);
	}
}

void dpc_prepare(dpc_t *dpc, dpcfn_t fn) {
	dpc->fn = fn;
	dpc->enqueued = false;
	dpc->list_node.next = NULL;
	dpc->list_node.prev = NULL;
}

void dpc_enqueue(dpc_t *dpc, dpcarg_t arg) {
	bool entrystate = interrupt_set(false);

	if (dpc->enqueued)
		goto cleanup;

	dpc->arg = arg;
	dpc->enqueued = true;
	list_push_front(&current_cpu()->dpcqueue, &dpc->list_node);
	interrupt_raise(current_cpu()->dpcisr);

	cleanup:
	interrupt_set(entrystate);
}

void dpc_dequeue(dpc_t *dpc) {
	bool entrystate = interrupt_set(false);

	if (dpc->enqueued == false)
		goto cleanup;

	dpc->enqueued = false;
	list_remove(&dpc->list_node);

	cleanup:
	interrupt_set(entrystate);
}

void dpc_init() {
	list_init(&current_cpu()->dpcqueue);
	current_cpu()->dpcisr = interrupt_allocate(isrfn, NULL, IPL_DPC);
	__assert(current_cpu()->dpcisr);
}
