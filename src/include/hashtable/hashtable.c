#include <hashtable.h>
#include <string.h>
#include <kernel/alloc.h>

// temporary for testing

size_t hash(char* ptr, size_t size){
	size_t hash = 0;
	for(size_t i = 0; i < size; ++i){
		hash += ptr[i] * 9;
	}

	return hash % 883;

}

bool hashtable_init(hashtable* table, size_t size){
	table->entries = alloc(size*sizeof(hashtableentry));
	if(!table->entries) return false;
	table->size = size;
	return true;
}

static inline hashtableentry* getentry(hashtable* table, char* key){
	size_t entryn = hash(key, strlen(key)) % table->size;

	return &table->entries[entryn];
}

bool hashtable_insert(hashtable* table, char* key, void* val){

	
	hashtableentry* entry = getentry(table, key);

	char* keysave = alloc(strlen(key)+1);

	memcpy(keysave, key, strlen(key)+1);

	if(!keysave) return false;

	if(entry->key){
		while(entry->next)
			entry = entry->next;

		entry->next = alloc(sizeof(hashtableentry));

		if(!entry->next){
			free(keysave);
			return false;
		}

		entry = entry->next;
	}
	
	entry->key = keysave;
	entry->val = val;
	
	return true;

}

bool hashtable_remove(hashtable* table, char* key){
	
	hashtableentry* entry = getentry(table, key);
	
	if(!entry->key) return false;
	
	hashtableentry* prev = NULL;
	
	while(entry){
		if(!strcmp(entry->key, key))
			break;
		prev = entry;
		entry = entry->next;
	}
	
	if(!entry) return false;

	if(prev){
		prev->next = entry->next;
		free(entry->key);
		free(entry);
	}
	else{
		if(entry->next){
			void* addr = entry->next;
			memcpy(entry, entry->next, sizeof(hashtableentry));
			free(addr);
		}
		else{
			memset(entry, 0, sizeof(hashtableentry));
		}
		return true;
	}


	return true;

}

void* hashtable_get(hashtable* table, char* key){
	
	hashtableentry* entry = getentry(table, key);
	
	if(!entry->key)
		return NULL;

	while(entry && strcmp(entry->key, key))
		entry = entry->next;

	if(!entry) return NULL;
	
	return entry->val;
}
