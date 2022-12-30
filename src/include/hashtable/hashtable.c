#include <hashtable.h>
#include <string.h>
#include <kernel/alloc.h>

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
	if(!table->size)
		return NULL;
	size_t entryn = hash(key, strlen(key)) % table->size;

	return &table->entries[entryn];
}

bool hashtable_isset(hashtable* table, char* key){
	
	hashtableentry* entry = getentry(table, key);
	
	if(!entry)
		return false;

	if(!entry->key)
		return false;

	while(entry && strcmp(entry->key, key))
		entry = entry->next;

	if(!entry) return false;
	
	return true;
}

void* hashtable_fromoffset(hashtable* table, uintmax_t offset, char* retkey){
		
	for(uintmax_t i = 0; i < table->size; ++i){
		hashtableentry* entry = &table->entries[i];
		if(!entry->val)
			continue;
		while(entry){
			if(offset-- == 0){
				if(retkey)
					strcpy(retkey, entry->key);
				return entry->val;
			}

			entry = entry->next;
		}
	}

	return NULL;
		
}

bool hashtable_insert(hashtable* table, char* key, void* val){

	
	hashtableentry* entry = getentry(table, key);

	if(!entry)
		return NULL;

	char* keysave = alloc(strlen(key)+1);

	if(!keysave) return false;

	memcpy(keysave, key, strlen(key)+1);

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
	++table->entrycount;

	return true;

}

bool hashtable_remove(hashtable* table, char* key){
	
	hashtableentry* entry = getentry(table, key);
	
	if(!entry) return false;

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
	}

	--table->entrycount;

	return true;

}

bool hashtable_set(hashtable* table, char* key, void* value){
	hashtableentry* entry = getentry(table, key);
	
	if(!entry)
		return false;

	if(!entry->key)
		return false;

	while(entry && strcmp(entry->key, key))
		entry = entry->next;

	if(!entry) return false;
	
	entry->val = value;

	return true;
}

void* hashtable_get(hashtable* table, char* key){
	
	hashtableentry* entry = getentry(table, key);
	
	if(!entry)
		return NULL;

	if(!entry->key)
		return NULL;

	while(entry && strcmp(entry->key, key))
		entry = entry->next;

	if(!entry) return NULL;
	
	return entry->val;
}

void hashtable_destroy(hashtable* table){
	
	// TODO fix memory leak bruh
	
	free(table->entries);


}
