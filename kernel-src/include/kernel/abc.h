#ifndef _ABC_H
#define _ABC_H

#include <list.h>
#include <stdbool.h>
#include <trie.h>
#include <mutex.h>
#include <kernel/vfs.h>
#include <kernel/thread.h>
#include <kernel/event.h>

#define ABC_BLOCK_FLAGS_DIRTY 1
#define ABC_BLOCK_FLAGS_BUSY 2
typedef struct {
	list_node_t dirty_list_node;
	uint64_t block;
	void *data;
	size_t refcount;
	int flags;
} abc_block_t;

typedef struct {
	mutex_t mutex; // TODO: use pushlock properly here once pushlock rewrite is known good
	trie_t blocks;
	size_t block_size;

	mutex_t dirty_list_mutex;
	list_t dirty_list;
	bool syncing;
	eventheader_t dirty_list_sync_event;
	eventheader_t dirty_list_empty_event;

	vnode_t *backing;
	thread_t *writer;
} abc_t;

int abc_init(abc_t *abc, vnode_t *vnode, size_t block_size);
int abc_get_block(abc_t *abc, uint64_t block, abc_block_t **ret);
void abc_make_dirty(abc_t *abc, abc_block_t *block);
void abc_sync(abc_t *abc);
void abc_release_block(abc_t *abc, abc_block_t *abc_block);

#endif
