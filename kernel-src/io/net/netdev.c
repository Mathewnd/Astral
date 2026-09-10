#include <kernel/net.h>
#include <kernel/devfs.h>
#include <hashtable.h>
#include <mutex.h>
#include <string.h>
#include <logging.h>
#include <kernel/usercopy.h>
#include <kernel/sock.h>
#include <kernel/wlan.h>

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

typedef struct {
	void *buffer;
	size_t buffer_size;
	size_t records_returned; // filled in by kernel
} wlan_bss_request_t;

typedef struct {
	uint8_t bssid[6];
	void *ie;
	size_t ie_size;
} wlan_association_request_t;

#define MAX_IE_SIZE 1024

#define NETDEV_IOCTL_GET_INFO 0x1337631
#define NETDEV_IOCTL_WLAN_SCAN 0x8021101
#define NETDEV_IOCTL_WLAN_SCAN_WAIT 0x8021102
#define NETDEV_IOCTL_WLAN_GET_BSS_CACHE 0x8021103
#define NETDEV_IOCTL_WLAN_ASSOCIATE 0x8021104
#define NETDEV_IOCTL_WLAN_ASSOCIATION_WAIT 0x8021105
#define NETDEV_IOCTL_WLAN_DISASSOCIATE 0x8021106
#define NETDEV_IOCTL_WLAN_SET_KEY 0x8021107
#define NETDEV_IOCTL_WLAN_DEL_KEY 0x8021108

int handle_wlan_ioctl(netdev_t *netdev, unsigned long request, void *arg, int *result, cred_t *cred) {
	// TODO: make every operation besides get bss cache be privileged
	switch (request) {
		case NETDEV_IOCTL_WLAN_SCAN:
			return wlan_active_scan(netdev);
		case NETDEV_IOCTL_WLAN_SCAN_WAIT:
			return wlan_wait_for_scan(netdev);
		case NETDEV_IOCTL_WLAN_GET_BSS_CACHE: {
			wlan_bss_request_t bss_request;
			int error = USERCOPY_POSSIBLY_FROM_USER(&bss_request, arg, sizeof(bss_request));
			if (error)
				return error;

			if (IS_USER_ADDRESS(arg) && !IS_USER_ADDRESS(bss_request.buffer))
				return EFAULT;

			wlan_get_bss_cache(netdev, bss_request.buffer, bss_request.buffer_size, &bss_request.records_returned);
			return USERCOPY_POSSIBLY_TO_USER(arg, &bss_request, sizeof(bss_request));
		}
		case NETDEV_IOCTL_WLAN_ASSOCIATE: {
			wlan_association_request_t ar;
			int error = USERCOPY_POSSIBLY_FROM_USER(&ar, arg, sizeof(ar));
			if (error)
				return error;

			if (IS_USER_ADDRESS(arg) && !IS_USER_ADDRESS(ar.ie))
				return EFAULT;

			if (ar.ie_size > MAX_IE_SIZE)
				return EINVAL;

			void *ie = alloc(ar.ie_size);
			if (ie == NULL)
				return ENOMEM;

			error = USERCOPY_POSSIBLY_FROM_USER(ie, ar.ie, ar.ie_size);
			if (error) {
				free(ie);
				return error;
			}

			error = wlan_associate(netdev, ar.bssid, ie, ar.ie_size);
			free(ie);
			return error;
		}
		case NETDEV_IOCTL_WLAN_ASSOCIATION_WAIT:
			return wlan_associate_wait(netdev);
		case NETDEV_IOCTL_WLAN_DISASSOCIATE:
			return wlan_disassociate(netdev);
		case NETDEV_IOCTL_WLAN_SET_KEY:

			break;
		case NETDEV_IOCTL_WLAN_DEL_KEY:

			break;
		default:
			return ENOTTY;
	}
}

static int netdev_ioctl(int minor, unsigned long request, void *arg, int *result, cred_t *cred) {
	netdev_t *netdev = minors[minor];
	if (netdev == NULL)
		return ENODEV;

	switch (request) {
		case NETDEV_IOCTL_GET_INFO: {
			net_info_t *info = arg;
			sockaddr_t addr = {0};
			abisockaddr_t abisockaddr;
			abisockaddr_t empty_addr = {0};
			short flags = netdev->flags;
			int mtu = netdev->mtu;
			short hwaddr_type = (netdev->flags & NETDEV_FLAGS_LOOPBACK) ? ARPHRD_LOOPBACK : ARPHRD_ETHER;

			addr.ipv4addr.addr = netdev->ip;
			sock_addrtoabiaddr(SOCKET_TYPE_UDP, &addr, &abisockaddr);
			int error = USERCOPY_POSSIBLY_TO_USER(&info->addr, &abisockaddr, sizeof(abisockaddr));
			if (error)
				return error;

			error = USERCOPY_POSSIBLY_TO_USER(&info->broadaddr, &empty_addr, sizeof(empty_addr));
			if (error)
				return error;

			error = USERCOPY_POSSIBLY_TO_USER(&info->netmask, &empty_addr, sizeof(empty_addr));
			if (error)
				return error;

			error = USERCOPY_POSSIBLY_TO_USER(&info->hwaddr, &empty_addr, sizeof(empty_addr));
			if (error)
				return error;

			error = USERCOPY_POSSIBLY_TO_USER(&info->hwaddr.type, &hwaddr_type, sizeof(hwaddr_type));
			if (error)
				return error;

			error = USERCOPY_POSSIBLY_TO_USER(info->hwaddr.addr, netdev->mac.address, sizeof(mac_t));
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
			return handle_wlan_ioctl(netdev, request, arg, result, cred);
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
