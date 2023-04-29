#include <hashtable.h>
#include <kernel/alloc.h>
#include <kernel/slab.h>
#include <errno.h>
#include <string.h>

#define FNV1PRIME  0x100000001b3ull
#define FNV1OFFSET 0xcbf29ce484222325ull

static scache_t *hashentrycache;

// FNV-1a hash implementation
static uint64_t hashbuffer(void *buffer, size_t size) {
	uint8_t *ptr = buffer;
	uint8_t *top = ptr + size;
	uint64_t h = FNV1OFFSET;

	while (ptr < top) {
		h ^= *ptr++;
		h *= FNV1PRIME;
	}

	return h;
}
static hashentry_t *getentry(hashtable_t *table, void *key, size_t keysize, uintmax_t hash) {
	uintmax_t tableoffset = hash % table->capacity;

	hashentry_t *entry = table->entries[tableoffset];

	while (entry) {
		if (entry->keysize == keysize && entry->hash == hash && memcmp(entry->key, key, keysize) == 0)
			break;
		entry = entry->next;
	}

	return entry;
}

int hashtable_set(hashtable_t *table, void *value, void *key, size_t keysize, bool allocate) {
	uintmax_t hash = hashbuffer(key, keysize);

	hashentry_t *entry = getentry(table, key, keysize, hash);

	if (entry) {
		entry->value = value;
		return 0;
	} else if (allocate) {
		uintmax_t tableoffset = hash % table->capacity;

		entry = slab_allocate(hashentrycache);
		if (entry == NULL)
			return ENOMEM;
		entry->key = alloc(keysize);
		if (entry->key == NULL) {
			slab_free(hashentrycache, entry);
			return ENOMEM;
		}

		memcpy(entry->key, key, keysize);
		entry->prev = NULL;
		entry->next = table->entries[tableoffset];
		entry->value = value;
		entry->keysize = keysize;
		entry->hash = hash;
		table->entries[tableoffset] = entry;
		return 0;
	}

	return ENOENT;
}

int hashtable_get(hashtable_t *table, void **value, void *key, size_t keysize) {
	uintmax_t hash = hashbuffer(key, keysize);

	hashentry_t *entry = getentry(table, key, keysize, hash);

	if (entry == NULL)
		return ENOENT;

	*value = entry->value;
	return 0;
}

int hashtable_remove(hashtable_t *table, void *key, size_t keysize) {
	uintmax_t hash = hashbuffer(key, keysize);

	hashentry_t *entry = getentry(table, key, keysize, hash);

	if (entry == NULL)
		return ENOENT;

	if (entry->prev)
		entry->prev->next = entry->next;

	if (entry->next)
		entry->next->prev = entry->prev;

	free(entry->key);
	slab_free(hashentrycache, entry);

	return 0;
}

int hashtable_destroy(hashtable_t *table) {
	// as to avoid using freed data, keep a copy of the entry
	hashentry_t entrysave;
	HASHTABLE_FOREACH(table) {
		entrysave = *entry;
		free(entry->key);
		slab_free(hashentrycache, entry);
		entry = &entrysave;
	}
	return 0;
}

int hashtable_init(hashtable_t *table, size_t size) {
	// make sure the cache is initialised
	if (hashentrycache == NULL) {
		hashentrycache = slab_newcache(sizeof(hashentry_t), 0, NULL, NULL);
		if (hashentrycache == NULL)
			return ENOMEM;
	}

	table->capacity = size;
	table->entries = alloc(size * sizeof(hashentry_t *));
	if (table->entries == NULL)
		return ENOMEM;

	return 0;
}
