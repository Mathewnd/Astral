#ifndef _ENV_H_INCLUDE
#define _ENV_H_INCLUDE

#include <stdbool.h>

int env_set(const char*, const char*);
char* env_get(const char*);
bool env_isset(const char*);
void env_init();

#endif
