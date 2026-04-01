#include <kernel/net.h>
#include <kernel/devfs.h>
#include <hashtable.h>
#include <mutex.h>
#include <string.h>
#include <logging.h>
#include <kernel/usercopy.h>
#include <kernel/sock.h>

static int current_minor;
static netdev_t *minors[64];

HASHTABLE_DEFINE_STATIC(nametable, 10);
static MUTEX_DEFINE(tablelock);

typedef struct {
	abisockaddr_t addr;
	abisockaddr_t broadaddr;
	abisockaddr_t netmask;
	abisockaddr_t hwaddr;
	short flags;
	int mtu;
} net_info_t;

static int netdev_ioctl(int minor, unsigned long request, void *arg, int *result, cred_t *cred) {
	netdev_t *netdev = minors[minor];
	if (netdev == NULL)
		return ENODEV;

	switch (request) {
		case 0x1337631: {
			net_info_t *info = arg;
			sockaddr_t addr = {0};
			abisockaddr_t abisockaddr;
			short flags = netdev->flags;
			int mtu = netdev->mtu;

			addr.ipv4addr.addr = netdev->ip;
			sock_addrtoabiaddr(SOCKET_TYPE_UDP, &addr, &abisockaddr);
			int error = USERCOPY_POSSIBLY_TO_USER(&info->addr, &abisockaddr, sizeof(abisockaddr));
			if (error)
				return error;

			error = USERCOPY_POSSIBLY_TO_USER(&info->hwaddr, &netdev->mac, sizeof(netdev->mac));
			if (error)
				return error;

			error = USERCOPY_POSSIBLY_TO_USER(&info->mtu, &mtu, sizeof(mtu));
			if (error)
				return error;

			error = USERCOPY_POSSIBLY_TO_USER(&info->flags, &flags, sizeof(flags));
			if (error)
				return error;

			// TODO broadaddr
			// TODO netmask

			break;
		}
		default:
			return ENOTTY;
	}

	return 0;
}

static devops_t devops = {
	.ioctl = netdev_ioctl
};

int netdev_register(netdev_t *netdev, char *name) {
	int minor = __atomic_fetch_add(&current_minor, 1, __ATOMIC_RELAXED);
	MUTEX_ACQUIRE(&tablelock);
	int e = hashtable_set(&nametable, netdev, name, strlen(name), true);
	if (e)
		goto leave;


	e = devfs_register(&devops, name, V_TYPE_CHDEV, DEV_MAJOR_NET, minor, 0600, NULL);
	if (e)
		hashtable_remove(&nametable, name, strlen(name));

	minors[minor] = netdev;

	leave:
	MUTEX_RELEASE(&tablelock);
	return e;
}

netdev_t *netdev_getdev(char *name) {
	MUTEX_ACQUIRE(&tablelock);
	netdev_t *netdev = NULL;
	void *tmp;

	if (hashtable_get(&nametable, &tmp, name, strlen(name)) == 0)
		netdev = tmp;

	MUTEX_RELEASE(&tablelock);
	return netdev;
}
