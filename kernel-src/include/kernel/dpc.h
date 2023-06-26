#ifndef _DPC_H
#define _DPC_H

#include <arch/context.h>
#include <stdbool.h>

typedef void * dpcarg_t;
typedef void (*dpcfn_t)(context_t *, dpcarg_t);

typedef struct _dpc_t {
	struct _dpc_t *next;
	struct _dpc_t *prev;
	bool enqueued;
	dpcfn_t fn;
	dpcarg_t arg;
} dpc_t;

void dpc_enqueue(dpc_t *dpc, dpcfn_t, dpcarg_t);
void dpc_dequeue(dpc_t *dpc);
void dpc_init();

#endif
