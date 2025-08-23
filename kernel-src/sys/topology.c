#include <kernel/topology.h>
#include <kernel/slab.h>
#include <logging.h>
#include <mutex.h>
#include <spinlock.h>
#include <util.h>
#include <kernel/scheduler.h>

static scache_t *slab_cache;
topology_node_t topology_root;
static MUTEX_DEFINE(allocate_mutex);
static SPINLOCK_DEFINE(tree_lock);

topology_node_t *topology_create_node(void) {
	MUTEX_ACQUIRE(&allocate_mutex);
	if (unlikely(slab_cache == NULL)) {
		slab_cache = slab_newcache(sizeof(topology_node_t), 0, NULL, NULL);
		__assert(slab_cache);
	}

	topology_node_t *node = slab_allocate(slab_cache);
	MUTEX_RELEASE(&allocate_mutex);
	return node;
}

topology_node_t *topology_find_child_by_id(topology_node_t *node, int id) {
	bool status = spinlock_acquire_irq_clear(&tree_lock);
	topology_node_t *child = node->children;

	while (child && child->id != id)
		child = child->sibling;

	spinlock_release_irq_restore(&tree_lock, status);
	return child;
}

void topology_insert(topology_node_t *node, topology_node_t *parent, int id, cpu_t *cpu) {
	bool status = spinlock_acquire_irq_clear(&tree_lock);

	node->id = id;
	node->cpu = cpu;

	node->parent = parent;
	node->sibling = node->parent->children;
	node->parent->children = node;
	node->children = NULL;

	spinlock_release_irq_restore(&tree_lock, status);
}

static cpu_t *search_find_leaf_to_run(topology_node_t *cpu, thread_t *thread);

static cpu_t *search_node(topology_node_t *node, thread_t *thread) {
	if (node->children)
		return search_find_leaf_to_run(node, thread);

	// deliberately racey to reduce contention
	return sched_thread_can_run_in_cpu(thread, node->cpu->last_queue, node->cpu->last_interactivity) ? node->cpu : NULL;
}

static cpu_t *search_find_leaf_to_run(topology_node_t *cpu, thread_t *thread) {
	for (topology_node_t *child = cpu->children; child; child = child->sibling) {
		cpu_t *result = search_node(child, thread);
		if (result)
			return result;
	}

	return NULL;
}

static cpu_t *search_internal_recursive_up(topology_node_t *cpu, topology_node_t *ignore, thread_t *thread, time_t sleep_time, int depth) {
	if (cpu == topology_get_root())
		return NULL;
	if (sleep_time > (SCHED_SLEEP_TIME_LIMIT_CPU_US * depth))
		return search_internal_recursive_up(cpu->parent, cpu, thread, sleep_time, depth + 1);

	for (topology_node_t *child = cpu->children; child; child = child->sibling) {
		if (child == ignore)
			continue;

		cpu_t *result = search_node(child, thread);
		if (result)
			return result;
	}

	return search_internal_recursive_up(cpu->parent, cpu, thread, sleep_time, depth + 1);
}

cpu_t *topology_find_next_cpu_to_run(topology_node_t *last_cpu, thread_t *thread, time_t sleep_time) {
	if (sleep_time <= SCHED_SLEEP_TIME_LIMIT_CPU_US && sched_thread_can_run_in_cpu(thread, last_cpu->cpu->last_queue, last_cpu->cpu->last_interactivity))
		return last_cpu->cpu;

	bool status = spinlock_acquire_irq_clear(&tree_lock);

	cpu_t *cpu = search_internal_recursive_up(last_cpu->parent, last_cpu, thread, sleep_time, 2);

	spinlock_release_irq_restore(&tree_lock, status);
	return cpu;
}
