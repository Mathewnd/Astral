#ifndef _HASHTABLE_H_INCLUDE
#define _HASHTABLE_H_INCLUDE

#include <stddef.h>
#include <stdbool.h>

typedef struct{
	void* next;
	char* key;
	void* val;
} hashtableentry;

typedef struct{
	size_t size;
	hashtableentry* entries;
} hashtable;

bool hashtable_init(hashtable*, size_t);
bool hashtable_insert(hashtable*, char*, void*);
bool hashtable_remove(hashtable*, char*);
void hashtable_destroy(hashtable*);
void* hashtable_get(hashtable*, char*);

#endif
