#include <kernel/net.h>
#include <kernel/devfs.h>
#include <hashtable.h>
#include <mutex.h>
#include <string.h>
#include <logging.h>

static hashtable_t nametable;
static mutex_t tablelock;

static devops_t devops = {0};

int netdev_register(netdev_t *netdev, char *name) {
	MUTEX_ACQUIRE(&tablelock, false);
	int e = hashtable_set(&nametable, netdev, name, strlen(name), true);
	if (e)
		goto leave;


	e = devfs_register(&devops, name, V_TYPE_CHDEV, DEV_MAJOR_NET, 0, 0644);
	if (e)
		hashtable_remove(&nametable, name, strlen(name));

	leave:
	MUTEX_RELEASE(&tablelock);
	return e;
}

netdev_t *netdev_getdev(char *name) {
	MUTEX_ACQUIRE(&tablelock, false);
	netdev_t *netdev = NULL;
	void *tmp;

	if (hashtable_get(&nametable, &tmp, name, strlen(name)) == 0)
		netdev = tmp;

	MUTEX_RELEASE(&tablelock);
	return netdev;
}

void netdev_init() {
	__assert(hashtable_init(&nametable, 10) == 0);
	MUTEX_INIT(&tablelock);
}
