#include <trie.h>
#include <errno.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <kernel/alloc.h>
#include <kernel/init.h>
#include <kernel/slab.h>
#include <logging.h>

static scache_t *trie_node_cache;

static void trie_cache_init(void) {
	trie_node_cache = slab_newcache(sizeof(trie_node_t), 0, NULL, NULL);
	__assert(trie_node_cache);
}

INIT_ROUTINE_DEFINE(trie, INIT_ROUTINE_FLAGS_NONE, trie_cache_init, slab);

void trie_init(trie_t *trie) {
	trie->height = 0;
	trie->root = NULL;
}

static inline uint8_t key_height(uint64_t key) {
	if (key == 0)
		return 0;

	return (64 - __builtin_clzll(key) + TRIE_BITS - 1) / TRIE_BITS;
}

static inline uint8_t get_offset(uint64_t key, uint8_t height) {
	return (key >> (height * TRIE_BITS)) & (TRIE_NODE_COUNT - 1);
}

static trie_node_t *trie_allocate_node(trie_preallocation_t *preallocation) {
	if (preallocation == NULL)
		return slab_allocate(trie_node_cache);

	if (preallocation->allocation_count == 0)
		return NULL;

	--preallocation->allocation_count;
	trie_node_t *node = preallocation->allocations[preallocation->allocation_count];
	preallocation->allocations[preallocation->allocation_count] = NULL;
	return node;
}

static void trie_release_node(trie_preallocation_t *preallocation, trie_node_t *node) {
	if (preallocation == NULL) {
		slab_free(trie_node_cache, node);
		return;
	}

	__assert(preallocation->allocation_count < TRIE_MAX_DEPTH);
	preallocation->allocations[preallocation->allocation_count] = node;
	++preallocation->allocation_count;
}

trie_preallocation_t *trie_preallocate(void) {
	trie_preallocation_t *preallocation = alloc(sizeof(trie_preallocation_t));
	if (preallocation == NULL)
		return NULL;

	while (preallocation->allocation_count < TRIE_MAX_DEPTH) {
		preallocation->allocations[preallocation->allocation_count] = slab_allocate(trie_node_cache);
		if (preallocation->allocations[preallocation->allocation_count] == NULL) {
			trie_free_preallocation(preallocation);
			return NULL;
		}

		++preallocation->allocation_count;
	}

	return preallocation;
}

void trie_free_preallocation(trie_preallocation_t *preallocation) {
	for (size_t i = 0; i < preallocation->allocation_count; ++i)
		slab_free(trie_node_cache, preallocation->allocations[i]);

	free(preallocation);
}

static void trie_trim(trie_t *trie) {
	if (trie->root->count == 0) {
		slab_free(trie_node_cache, trie->root);
		trie->root = NULL;
		trie->height = 0;
	} else while (trie->height && trie->root->count == 1 && trie->root->nodes[0]) {
		--trie->height;
		void *old_root = trie->root;
		trie->root = trie->root->nodes[0];
		slab_free(trie_node_cache, old_root);
	}
}

static int trie_insert_internal(trie_node_t *trie_node, uint64_t key, void *value, uint8_t height, trie_preallocation_t *preallocation) {
	uint8_t offset = get_offset(key, height);
	if (height == 0) {
		if (trie_node->items[offset])
			return EEXIST;

		trie_node->items[offset] = value;
		++trie_node->count;
		return 0;
	}

	if (trie_node->nodes[offset] == NULL) {
		trie_node->nodes[offset] = trie_allocate_node(preallocation);
		if (trie_node->nodes[offset] == NULL)
			return ENOMEM;

		memset(trie_node->nodes[offset], 0, sizeof(trie_node_t));

		trie_node->nodes[offset]->count = 0;
		++trie_node->count;
	}

	int error = trie_insert_internal(trie_node->nodes[offset], key, value, height - 1, preallocation);
	if (error && trie_node->nodes[offset]->count == 0) {
		trie_release_node(preallocation, trie_node->nodes[offset]);
		trie_node->nodes[offset] = NULL;
		--trie_node->count;
	}

	return error;
}

static bool expand_trie(trie_t *trie, uint8_t difference, trie_preallocation_t *preallocation) {
	if (difference == 0)
		return true;

	trie_node_t *new = trie_allocate_node(preallocation);
	if (new == NULL)
		return false;

	memset(new, 0, sizeof(trie_node_t));

	if (trie->root) {
		new->nodes[0] = trie->root;
		new->count = 1;
	}
	trie->root = new;
	++trie->height;

	if (!expand_trie(trie, difference - 1, preallocation)) {
		trie->root = new->nodes[0];
		--trie->height;
		trie_release_node(preallocation, new);
		return false;
	}

	return true;
}

static void undo_expand(trie_t *trie, uint8_t difference, trie_preallocation_t *preallocation) {
	if (difference == 0)
		return;

	trie_node_t *trie_node = trie->root;
	trie->root = trie_node->nodes[0];
	trie_release_node(preallocation, trie_node);
	--trie->height;

	undo_expand(trie, difference - 1, preallocation);
}

static int trie_insert_with_preallocation(trie_t *trie, uint64_t key, void *value, trie_preallocation_t *preallocation) {
	__assert(value);

	uint8_t height = key_height(key);

	if (height == 0 && trie->height == 0) {
		if (trie->root_item)
			return EEXIST;

		trie->root_item = value;
		return 0;
	}

	uint8_t difference = height > trie->height ? height - trie->height : 0;
	if (!expand_trie(trie, difference, preallocation))
		return ENOMEM;

	int error = trie_insert_internal(trie->root, key, value, trie->height - 1, preallocation);
	if (error)
		undo_expand(trie, difference, preallocation);

	return error;
}

int trie_insert(trie_t *trie, uint64_t key, void *value) {
	return trie_insert_with_preallocation(trie, key, value, NULL);
}

int trie_insert_preallocated(trie_t *trie, uint64_t key, void *value, trie_preallocation_t *preallocation) {
	__assert(preallocation);
	return trie_insert_with_preallocation(trie, key, value, preallocation);
}

static int trie_lookup_internal(trie_node_t *trie_node, uint64_t key, uint8_t height, void **ret) {
	if (trie_node == NULL)
		return ENOENT;

	if (height == 0) {
		*ret = trie_node->items[get_offset(key, height)];
		return *ret ? 0 : ENOENT;
	}

	return trie_lookup_internal(trie_node->nodes[get_offset(key, height)], key, height - 1, ret);
}

int trie_lookup(trie_t *trie, uint64_t key, void **ret) {
	uint8_t height = key_height(key);
	if (height > trie->height)
		return ENOENT;

	if (trie->height == 0) {
		*ret = trie->root_item;
		return *ret ? 0 : ENOENT;
	}

	return trie_lookup_internal(trie->root, key, trie->height - 1, ret);
}

static int trie_remove_internal(trie_node_t *trie_node, uint64_t key, uint8_t height) {
	if (trie_node == NULL)
		return ENOENT;

	if (height == 0) {
		uint8_t offset = get_offset(key, height);
		if (trie_node->items[offset] == NULL)
			return ENOENT;

		trie_node->items[offset] = NULL;
		--trie_node->count;
		return 0;
	}

	int error = trie_remove_internal(trie_node->nodes[get_offset(key, height)], key, height - 1);
	if (error)
		return error;

	if (trie_node->nodes[get_offset(key, height)]->count == 0) {
		slab_free(trie_node_cache, trie_node->nodes[get_offset(key, height)]);
		trie_node->nodes[get_offset(key, height)] = NULL;
		--trie_node->count;
	}

	return 0;
}

int trie_remove(trie_t *trie, uint64_t key) {
	uint8_t height = key_height(key);
	if (height > trie->height)
		return ENOENT;

	if (trie->height == 0) {
		if (trie->root_item == NULL)
			return ENOENT;

		trie->root_item = NULL;
		return 0;
	}

	int error = trie_remove_internal(trie->root, key, trie->height - 1);
	if (error)
		return error;

	trie_trim(trie);

	return 0;
}

static void trie_recursively_free(trie_node_t *trie_node, uint8_t height) {
	if (trie_node == NULL)
		return;

	if (height) {
		for (uint8_t i = 0; i < TRIE_NODE_COUNT; ++i)
			trie_recursively_free(trie_node->nodes[i], height - 1);
	}

	slab_free(trie_node_cache, trie_node);
}

static void trie_truncate_internal(trie_node_t *trie_node, uint64_t max_key, uint8_t height) {
	uint8_t offset = get_offset(max_key, height);

	if (height) {
		trie_node_t *child_node = trie_node->nodes[offset];
		if (child_node) {
			trie_truncate_internal(child_node, max_key, height - 1);

			if (child_node->count == 0) {
				slab_free(trie_node_cache, child_node);
				trie_node->nodes[offset] = NULL;
				--trie_node->count;
			}
		}
	}

	for (uint8_t i = offset + (height == 0 ? 0 : 1); i < TRIE_NODE_COUNT; ++i) {
		if (trie_node->nodes[i] == NULL)
			continue;

		if (height)
			trie_recursively_free(trie_node->nodes[i], height - 1);

		trie_node->nodes[i] = NULL;
		--trie_node->count;
	}
}

void trie_truncate(trie_t *trie, uint64_t max_key) {
	if (key_height(max_key) > trie->height)
		return;

	if (max_key == 0) {
		if (trie->height)
			trie_recursively_free(trie->root, trie->height - 1);

		trie->root_item = NULL;
		trie->height = 0;
		return;
	}

	if (max_key == 1) {
		void *item;
		if (trie_lookup(trie, 0, &item))
			item = NULL;

		if (trie->height)
			trie_recursively_free(trie->root, trie->height - 1);

		trie->root_item = item;
		trie->height = 0;
		return;
	}

	trie_truncate_internal(trie->root, max_key, trie->height - 1);

	trie_trim(trie);
}

static void trie_iterate_internal(trie_node_t *trie_node, uint64_t min_key, uint64_t max_key, uint8_t height, trie_iterate_fn_t fn, bool edge_low, bool edge_high) {
	if (trie_node == NULL)
		return;

	for (uint8_t i = 0; i < TRIE_NODE_COUNT; ++i) {
		if ((edge_low && i < get_offset(min_key, height)) || (edge_high && i > get_offset(max_key, height))) 
			continue;

		if (height)
			trie_iterate_internal(trie_node->nodes[i], min_key, max_key, height - 1, fn, edge_low && i == get_offset(min_key, height), edge_high && i == get_offset(max_key, height));
		else if (trie_node->items[i])
			fn(trie_node->items[i]);
	}
}

void trie_iterate(trie_t *trie, uint64_t min_key, uint64_t max_key, trie_iterate_fn_t fn) {
	if (key_height(min_key) > trie->height || min_key > max_key)
		return;

	if (trie->height == 0) {
		if (min_key == 0 && trie->root_item)
			fn(trie->root_item);

		return;
	}

	trie_iterate_internal(trie->root, min_key, max_key, trie->height - 1, fn, true, key_height(max_key) <= trie->height);
}
