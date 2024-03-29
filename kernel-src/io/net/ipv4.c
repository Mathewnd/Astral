#include <kernel/net.h>
#include <arch/cpu.h>
#include <logging.h>

int ipv4_allocdesc(netdev_t *netdev, size_t requestedsize, netdesc_t *desc) {
	int e = netdev->allocdesc(requestedsize + sizeof(ipv4frame_t), desc);
	desc->curroffset += sizeof(ipv4frame_t);
	return e;
}

#define VERSION_LENGTH(v, l) (((v) << 4) | l)

#define FLAG_MF 1
// offset has to be a multiple of 8
#define FLAGS_FRAGOFFSET(flg, offset) (((flg) << 13) | ((offset) >> 3))

static int dispatch_fragment(netdev_t *netdev, netdesc_t originaldesc, netdesc_t fragdesc, 
	uintmax_t fragmentoffset, size_t fragmentlen, int id, uint32_t ip, int proto) {
	ipv4frame_t frame = {
		.version_length = VERSION_LENGTH(4, 5),
		.servicetype_ecn = 0,
		.packetlen = cpu_to_be_w(fragmentlen + sizeof(ipv4frame_t)),
		.id = cpu_to_be_w(0x1234), // TODO proper id allocation per netdev
		.flags_fragoffset = cpu_to_be_w(FLAGS_FRAGOFFSET((fragmentoffset + fragmentlen < originaldesc.size - originaldesc.curroffset - sizeof(ipv4frame_t)) ? FLAG_MF : 0, fragmentoffset)),
		.timetolive = 0xff,
		.protocol = proto,
		.framechecksum = 0xfefe, // TODO checksum
		.srcaddr = 0xffffffff, // TODO src addr
		.dstaddr = cpu_to_be_d(ip)
	};

	memcpy((void *)((uintptr_t)fragdesc.address + fragdesc.curroffset), &frame, sizeof(ipv4frame_t));
	memcpy((void *)((uintptr_t)fragdesc.address + fragdesc.curroffset + sizeof(ipv4frame_t)), originaldesc.address + originaldesc.curroffset + sizeof(ipv4frame_t) + fragmentoffset, fragmentlen);
	return netdev->sendpacket(netdev, fragdesc, NET_BROADCAST_MAC, ETH_PROTO_IP);
}

int ipv4_sendpacket(netdev_t *netdev, netdesc_t desc, uint32_t ip, int proto) {
	size_t datasize = desc.size - desc.curroffset;
	desc.curroffset -= sizeof(ipv4frame_t);
	__assert(desc.size - desc.curroffset <= 65535);
	
	size_t mtuheader = netdev->mtu - sizeof(ipv4frame_t);
	size_t devfragmentsize = mtuheader - (mtuheader % 8);
	size_t fragmentcount = datasize / devfragmentsize + 1;

	for (int i = 0; i < fragmentcount; ++i) {
		size_t fragmentlen = (i == fragmentcount - 1) ? datasize - devfragmentsize * (fragmentcount - 1) : devfragmentsize;
		netdesc_t fragdesc;
		__assert(ipv4_allocdesc(netdev, fragmentlen + sizeof(ipv4frame_t), &fragdesc) == 0);
		fragdesc.curroffset -= sizeof(ipv4frame_t);
		dispatch_fragment(netdev, desc, fragdesc, i * devfragmentsize, fragmentlen, 0x1234, ip, proto);
		// TODO unallocate desc
	}

	return 0; // TODO error handling
}
