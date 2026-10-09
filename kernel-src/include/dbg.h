#ifndef _DBG_H
#define _DBG_H

#include <stddef.h>

void dbg_get_symbol(void *rip, char **fn_name, char **file_name, size_t *offset);

#endif
