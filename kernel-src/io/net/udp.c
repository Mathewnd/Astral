#include <kernel/net.h>
#include <arch/cpu.h>
#include <logging.h>

int udp_allocdesc(netdev_t *netdev, size_t requestedsize, netdesc_t *desc) {
	int e = netdev->allocdesc(requestedsize + sizeof(udpframe_t), desc);
	desc->curroffset += sizeof(udpframe_t);
	return e;
}

int udp_sendpacket(netdev_t *netdev, netdesc_t desc, uint32_t ip, uint16_t srcport, uint16_t dstport) {
	size_t len = desc.size - desc.curroffset;
	desc.curroffset -= sizeof(udpframe_t);
	udpframe_t frame = {
		.srcport = cpu_to_be_w(srcport),
		.dstport = cpu_to_be_w(dstport),
		.length = cpu_to_be_w(len),
		.checksum = 0xfefe
	};
	memcpy((void *)((uintptr_t)desc.address + desc.curroffset), &frame, sizeof(udpframe_t));

	return ipv4_sendpacket(netdev, desc, ip, IPV4_PROTO_UDP);
}
