#ifndef _TOPOLOGY_H
#define _TOPOLOGY_H

#define TOPOLOGY_MAKE_ID(depth, id) (((depth & 0xfffflu) << 16) | id)

#include <arch/cpu.h>

typedef struct topology_node_t {
	int id;
	struct topology_node_t *parent;
	struct topology_node_t *sibling;
	struct topology_node_t *children;
	cpu_t *cpu;
} topology_node_t;

extern topology_node_t topology_root;

topology_node_t *topology_create_node(void);
void topology_insert(topology_node_t *node, topology_node_t *parent, int id, cpu_t *cpu);
cpu_t *topology_find_next_cpu_to_run(topology_node_t *last_cpu, thread_t *thread, time_t sleep_time);
topology_node_t *topology_find_child_by_id(topology_node_t *node, int id);

static inline topology_node_t *topology_get_root(void) {
	return &topology_root;
}

#endif
