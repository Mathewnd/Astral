#include <kernel/net.h>
#include <kernel/eth.h>
#include <kernel/raw.h>
#include <logging.h>

// runs on dpc context, called by the individual driver dpcs on a receive.
void eth_process(netdev_t *netdev, void *buffer, size_t size) {
	if (size < sizeof(ethframe_t))
		return;

	ethframe_t *frame = buffer;

	mac_t dst, src;
	memcpy(&dst, &frame->destination, sizeof(mac_t));
	memcpy(&src, &frame->source, sizeof(mac_t));

	mac_t broadcast = NET_BROADCAST_MAC;
	// check if packet is for our machine or a broadcast. if neither, do nothing
	if (!(MAC_EQUAL(&dst, &netdev->mac) || MAC_EQUAL(&dst, &broadcast)))
		return;

	void *nextbuff = (void *)((uintptr_t)buffer + sizeof(ethframe_t));

	uint16_t ethertype = be_to_cpu_w(frame->type);
	switch (ethertype) {
		case ETH_PROTO_IP:
			ipv4_process(netdev, nextbuff, size - sizeof(ethframe_t));
			break;
		case ETH_PROTO_ARP:
			arp_process(netdev, nextbuff);
			break;
	}

	raw_process(netdev, nextbuff, &src, ethertype, size - sizeof(ethframe_t));
}
