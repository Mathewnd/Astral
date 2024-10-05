#include <kernel/topology.h>
#include <kernel/slab.h>
#include <logging.h>
#include <mutex.h>
#include <spinlock.h>
#include <util.h>

static scache_t *slab_cache;
topology_node_t topology_root;
static MUTEX_DEFINE(allocate_mutex);
static SPINLOCK_DEFINE(tree_lock);

topology_node_t *topology_create_node(void) {
	MUTEX_ACQUIRE(&allocate_mutex, false);
	if (unlikely(slab_cache == NULL)) {
		slab_cache = slab_newcache(sizeof(topology_node_t), 0, NULL, NULL);
		__assert(slab_cache);
	}

	topology_node_t *node = slab_allocate(slab_cache);
	MUTEX_RELEASE(&allocate_mutex);
	return node;
}

void topology_insert(topology_node_t *node, topology_node_t *parent, int id, cpu_t *cpu) {
	long old_ipl = spinlock_acquireraiseipl(&tree_lock, IPL_MAX);

	node->id = id;
	node->cpu = cpu;

	node->parent = parent;
	node->sibling = node->parent->children;
	node->parent->children = node;

	spinlock_releaseloweripl(&tree_lock, old_ipl);
}
