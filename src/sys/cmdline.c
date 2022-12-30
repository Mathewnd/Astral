#include <hashtable.h>
#include <limine.h>
#include <kernel/cmdline.h>
#include <kernel/env.h>
#include <kernel/alloc.h>
#include <arch/panic.h>
#include <string.h>
#include <stdio.h>

static volatile struct limine_kernel_file_request kfreq = {
	.id = LIMINE_KERNEL_FILE_REQUEST,
	.revision = 0
};

// takes a "EXAMPLE=EXAMPLE" pair, turns the '=' into a NULL and returns the start of the second token. returns NULL if there is no '='

static char* pair(char* s){
	size_t len = strlen(s);

	for(uintmax_t i = 0; i < len; ++i){
		if(s[i] == '='){
			s[i] = '\0';
			return s + i + 1;
		}
	}


	return NULL;
}

void cmdline_parse(){
	
	char* ptr = kfreq.response->kernel_file->cmdline;
	size_t count = *ptr == '\0' ? 0 : 1; 
	char* it = ptr;

	char** tokens = alloc(sizeof(char*));

	if(tokens == NULL)
		_panic("Out of memory!\n", NULL);

	*tokens = ptr;

	while(*it != '\0'){

		if(*it == ' '){
			*it = '\0';
			++count;
			tokens = realloc(tokens, sizeof(char*) * count);
			if(tokens == NULL)
				_panic("Out of memory!\n", NULL);
			tokens[count-1] = it+1;
		}

		++it;
	}

	for(uintmax_t i = 0; i < count; ++i){
		char* second = pair(tokens[i]);
		if(env_set(tokens[i], second))
			_panic("Out of memory!", NULL);
	}
	
	printf("cmdline: %lu envs\n", count);

	free(tokens);
		
}
