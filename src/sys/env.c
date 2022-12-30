#include <hashtable.h>
#include <errno.h>
#include <kernel/env.h>
#include <arch/panic.h>

static hashtable envtable;

int env_set(const char* name, const char* value){
	
	if(!hashtable_set(&envtable, name, value))
		if(!hashtable_insert(&envtable, name, value))
			return ENOMEM;
	
	return 0;

}

char* env_get(const char* name){
	return hashtable_get(&envtable, name);

}

bool env_isset(const char* name){
	return hashtable_isset(&envtable, name);
}

void env_init(){
	if(!hashtable_init(&envtable, 50))
		_panic("Out of memory!\n", NULL);
}
