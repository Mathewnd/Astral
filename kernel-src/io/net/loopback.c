#include <kernel/virtio.h>
#include <logging.h>
#include <kernel/pmm.h>
#include <kernel/eth.h>
#include <kernel/net.h>
#include <hashtable.h>
#include <arch/cpu.h>

static netdev_t loopbacknetdev;

static void rx_dpc(context_t *context, dpcarg_t arg) {
	eth_process(&loopbacknetdev, arg);
}

// requested size doesn't account for ethernet header or the virtio header
static int loopback_allocdesc(size_t requestedsize, netdesc_t *desc) {
	desc->address = alloc(requestedsize + sizeof(ethframe_t));
	if (desc->address == NULL)
		return ENOMEM;

	desc->size = requestedsize + sizeof(ethframe_t);
	desc->curroffset = sizeof(ethframe_t);
	return 0;
}

static int loopback_sendpacket(netdev_t *netdev, netdesc_t desc, mac_t targetmac, int proto) {
	desc.curroffset = 0;
	ethframe_t ethframe = {
		.type = cpu_to_be_w(proto)
	};

	mac_t broadcast = NET_BROADCAST_MAC;

	memcpy(&ethframe.source, &netdev->mac, sizeof(mac_t));
	memcpy(&ethframe.destination, &broadcast, sizeof(mac_t));
	memcpy(desc.address, &ethframe, sizeof(ethframe_t));

	dpc_t dpc = {0};
	dpc_enqueue(&dpc, rx_dpc, desc.address);

	return 0;
}

netdev_t *loopback_device() {
	return &loopbacknetdev;
}

void loopback_init() {
	loopbacknetdev.mtu = 30000;
	loopbacknetdev.sendpacket = loopback_sendpacket;
	loopbacknetdev.allocdesc = loopback_allocdesc;
	loopbacknetdev.ip = 0x7f000001;
	__assert(hashtable_init(&loopbacknetdev.arpcache, 30) == 0);

	for (int i = 0; i < 6; ++i)
		loopbacknetdev.mac.address[i] = 0;

	__assert(netdev_register(&loopbacknetdev, "lo") == 0);
}
