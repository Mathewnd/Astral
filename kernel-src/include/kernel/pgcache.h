#ifndef _PGCACHE_H
#define _PGCACHE_H

void pgcache_init();
int pgcache_getpage(vnode_t *node, uintmax_t offset, void **ret);
int pgcache_setdirty(void *addr);

#endif
