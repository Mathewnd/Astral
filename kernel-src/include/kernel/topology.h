#ifndef _TOPOLOGY_H
#define _TOPOLOGY_H

#define TOPOLOGY_MAKE_ID(depth, id) ((depth << 16) | id)

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

static inline topology_node_t *topology_get_root(void) {
	return &topology_root;
}

#endif
