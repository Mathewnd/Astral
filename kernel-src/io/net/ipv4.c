#include <kernel/net.h>
#include <kernel/eth.h>
#include <arch/cpu.h>
#include <logging.h>
#include <mutex.h>
#include <kernel/alloc.h>
#include <kernel/abi.h>

typedef struct {
	netdev_t *netdev;
	uint32_t addr;
	uint32_t gateway;
	uint32_t mask;
	int weight;
} routingentry_t;

static routingentry_t *routingtable;
static mutex_t routingtablelock;
static size_t routingtablesize;

static routingentry_t getroute(uint32_t ip) {
	routingentry_t selected = {0};

	MUTEX_ACQUIRE(&routingtablelock, false);

	for (int i = 0; i < routingtablesize; ++i) {
		if (routingtable[i].netdev == NULL || (routingtable[i].mask & ip) != (routingtable[i].addr & routingtable[i].mask))
			continue;

		selected = routingtable[i].weight > selected.weight ? routingtable[i] : selected;
	}

	MUTEX_RELEASE(&routingtablelock);

	return selected;
}

int ipv4_addroute(netdev_t *netdev, uint32_t addr, uint32_t gateway, uint32_t mask, int weight) {
	routingentry_t entry = {
		.netdev = netdev,
		.addr = addr,
		.gateway = gateway,
		.mask = mask,
		.weight = weight
	};

	MUTEX_ACQUIRE(&routingtablelock, false);

	int error = 0;
	int i;

	for (i = 0; i < routingtablesize; ++i) {
		if (routingtable[i].netdev != NULL)
			continue;

		routingtable[i] = entry;
		break;
	}

	if (i == routingtablesize) {
		// insert to the end
		size_t newsize = routingtablesize + 1;
		void *tmp = realloc(routingtable, newsize * sizeof(routingentry_t));
		if (tmp) {
			routingtable = tmp;
			routingtablesize = newsize;
			routingtable[i] = entry;
		} else {
			error = ENOMEM;
		}
	}

	MUTEX_RELEASE(&routingtablelock);

	return error;
}

#define VERSION_LENGTH(v, l) (((v) << 4) | l)

#define FLAG_MF 1
// offset has to be a multiple of 8
#define FLAGS_FRAGOFFSET(flg, offset) (((flg) << 13) | ((offset) >> 3))
#define GET_FLAGS(x) (((x) >> 13) & 0x7)
#define GET_FRAGOFFSET(x) ((x) & 0x1fff)

static int checksum(void *buffer, size_t size) {
	uint16_t *p = buffer;
	int sum = 0;
	int i;
	for (i = 0; i < (size & ~(1lu)); i += 2) {
		sum += be_to_cpu_w(p[i >> 1]);
	}

	if (size & 1) {
		sum += ((uint8_t *)p)[i];
	}

	sum = (sum >> 16) + (sum & 0xffff);
	sum += sum >> 16;

	uint16_t ret = ~sum;
	return ret;
}

static int dispatch_fragment(netdev_t *netdev, netdesc_t fragdesc, 
	uintmax_t fragmentoffset, size_t fragmentlen, int id, uint32_t ip, mac_t mac, int proto, bool last) {
	ipv4frame_t frame = {
		.version_length = VERSION_LENGTH(4, 5),
		.servicetype_ecn = 0,
		.packetlen = cpu_to_be_w(fragmentlen + sizeof(ipv4frame_t)),
		.id = cpu_to_be_w(0x1234), // TODO proper id allocation per netdev
		.flags_fragoffset = cpu_to_be_w(FLAGS_FRAGOFFSET(!last ? FLAG_MF : 0, fragmentoffset)),
		.timetolive = 0xff,
		.protocol = proto,
		.framechecksum = 0,
		.srcaddr = cpu_to_be_d(netdev->ip),
		.dstaddr = cpu_to_be_d(ip)
	};

	frame.framechecksum = cpu_to_be_w(checksum(&frame, sizeof(frame)));

	memcpy((void *)((uintptr_t)fragdesc.address + fragdesc.curroffset), &frame, sizeof(ipv4frame_t));
	return netdev->sendpacket(netdev, fragdesc, mac, ETH_PROTO_IP);
}

void ipv4_process(netdev_t *netdev, void *buff) {
	ipv4frame_t *frame = buff;

	if (checksum(frame, sizeof(ipv4frame_t)) != 0) {
		printf("ipv4: bad checksum\n");
		return;
	}

	if (frame->version_length != VERSION_LENGTH(4, 5))
		return;

	uint16_t flagsfrag = be_to_cpu_w(frame->flags_fragoffset);
	if ((GET_FLAGS(flagsfrag) & FLAG_MF) || (GET_FRAGOFFSET(flagsfrag) > 0)) {
		printf("ipv4: reassembly not yet supported!\n");
		return;
	}

	uint32_t dstip = be_to_cpu_d(frame->dstaddr);
	uint32_t srcip = be_to_cpu_d(frame->srcaddr);

	if (dstip != IPV4_BROADCAST_ADDRESS && dstip != netdev->ip)
		return;

	void *nextbuff = (void *)((uintptr_t)buff + sizeof(ipv4frame_t));

	switch (frame->protocol) {
		case IPV4_PROTO_UDP:
			udp_process(netdev, nextbuff, srcip);
			break;
		case IPV4_PROTO_TCP:
			tcp_process(netdev, nextbuff, frame);
			break;
	}
}

size_t ipv4_getmtu(uint32_t ip) {
	routingentry_t entry = getroute(ip);
	if (entry.netdev == NULL)
		return 0;

	return entry.netdev->mtu;
}

uint32_t ipv4_getnetdevip(uint32_t ip) {
	routingentry_t entry = getroute(ip);
	if (entry.netdev == NULL)
		return 0;

	return entry.netdev->ip;
}

int ipv4_sendpacket(void *buffer, size_t packetsize, uint32_t ip, int proto, netdev_t *broadcastdev) {
	if ((packetsize + sizeof(ipv4frame_t)) > 65535)
		return EMSGSIZE;

	mac_t mac;
	netdev_t *netdev;

	if (ip != IPV4_BROADCAST_ADDRESS) {
		routingentry_t entry = getroute(ip);
		if (entry.netdev == NULL)
			return ENETUNREACH;

		int error = arp_lookup(entry.netdev, entry.gateway ? entry.gateway : ip, &mac);
		if (error)
			return error;

		netdev = entry.netdev;
	} else {
		__assert(broadcastdev);
		mac = NET_BROADCAST_MAC;
		netdev = broadcastdev;
	}


	size_t mtuheader = netdev->mtu - sizeof(ipv4frame_t);
	size_t devfragmentsize = mtuheader - (mtuheader % 8);
	size_t fragmentcount = packetsize / devfragmentsize + 1;

	for (int i = 0; i < fragmentcount; ++i) {
		size_t fragmentlen = (i == fragmentcount - 1) ? packetsize - devfragmentsize * (fragmentcount - 1) : devfragmentsize;
		netdesc_t fragdesc;
		int e = netdev->allocdesc(netdev, fragmentlen + sizeof(ipv4frame_t), &fragdesc);
		if (e)
			return e;

		memcpy((void *)((uintptr_t)fragdesc.address + fragdesc.curroffset + sizeof(ipv4frame_t)), (void *)((uintptr_t)buffer + i * devfragmentsize), fragmentlen);

		e = dispatch_fragment(netdev, fragdesc, i * devfragmentsize, fragmentlen, 0x1234, ip, mac, proto, i == fragmentcount - 1);
		netdev->freedesc(netdev, &fragdesc);
		if (e)
			return e;
	}

	 return 0;
}

void ipv4_init() {
	routingtablesize = 1;
	MUTEX_INIT(&routingtablelock);
	routingtable = alloc(sizeof(routingentry_t));
	__assert(routingtable);
	__assert(ipv4_addroute(loopback_device(), 0x7f000001, 0, 0xff000000, 10000) == 0);
}
