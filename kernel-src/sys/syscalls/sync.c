#include <kernel/syscalls.h>
#include <kernel/vmmcache.h>

syscallret_t syscall_sync(context_t *) {
	syscallret_t ret = {0};
	vmmcache_sync();
	return ret;
}
