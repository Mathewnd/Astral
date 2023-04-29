#include <kernel/vfs.h>
#include <hashtable.h>
#include <string.h>
#include <logging.h>
#include <kernel/alloc.h>

static hashtable_t fstable;
vnode_t *vfsroot;

void vfs_init() {
	__assert(hashtable_init(&fstable, 20) == 0);
	vfsroot = alloc(sizeof(vnode_t));
	__assert(vfsroot);
}

int vfs_register(vfsops_t *ops, char *name) {
	return hashtable_set(&fstable, ops, name, strlen(name), true);
}

int vfs_mount(vnode_t *mounton, char *name, void *data) {
	vfsops_t *ops;
	int e = hashtable_get(&fstable, (void **)(&ops), name, strlen(name));
	if (e)
		return e;

	return 0;
}
