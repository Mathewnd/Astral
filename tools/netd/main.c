#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <poll.h>
#include <net/route.h>

#ifndef SIOCADDRT
	#define SIOCADDRT	0x890B
#endif

#define DHCPHDR_TYPE_CLIENT 1
#define DHCPHDR_TYPE_SERVER 2
#define DHCPHDR_HTYPE_ETH 1
#define DHCPHDR_HLEN_ETH 6
#define DHCPHDR_COOKIE 0x63825363
#define DHCPHDR_XID 0x12345678

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67

typedef struct {
	uint8_t op;
	uint8_t htype;
	uint8_t hlen;
	uint8_t hops;
	uint32_t xid;
	uint16_t secs;
	uint16_t flags;
	uint32_t clientip;
	uint32_t responseip;
	uint32_t serverip;
	uint32_t relayip;
	uint8_t clienthw[16];
	uint8_t sname[64];
	uint8_t file[128];
	uint32_t cookie;
} __attribute__((packed)) dhcphdr_t;

#define OPTION_TYPE 53
#define OPTION_TYPE_LENGTH 1
#define OPTION_TYPE_DHCPDISCOVER 1
#define OPTION_TYPE_DHCPOFFER 2
#define OPTION_TYPE_DHCPREQUEST 3
#define OPTION_TYPE_DHCPACK 5

#define OPTION_REQUEST 55
#define OPTION_REQUEST_LENGTH 2

#define OPTION_MASK 1
#define OPTION_ROUTER 3
#define OPTION_LEASETIME 51
#define OPTION_IPREQ 50
#define OPTION_SERVER 54
#define OPTION_END 0xff

static uint8_t discoveropts[] = {
	OPTION_TYPE, OPTION_TYPE_LENGTH, OPTION_TYPE_DHCPDISCOVER,
	OPTION_REQUEST, OPTION_REQUEST_LENGTH, OPTION_MASK, OPTION_ROUTER,
	0xff
};

typedef struct {
	dhcphdr_t hdr;
	uint8_t opts[sizeof(discoveropts)];
} __attribute__((packed)) dhcpdiscover_t;

typedef struct {
	dhcphdr_t hdr;
	uint8_t opts[];
} __attribute__((packed)) dhcpoffer_t;

typedef struct {
	uint8_t type[3];
	uint8_t ipreqhdr[2];
	uint32_t ipreq;
	uint8_t dhcpserverhdr[2];
	uint32_t dhcpserver;
	uint8_t end;
} __attribute__((packed)) requestopts_t;

typedef struct {
	dhcphdr_t hdr;
	requestopts_t opts;
} __attribute__((packed)) dhcprequest_t;

typedef struct {
	dhcphdr_t hdr;
	uint8_t opts[];
} __attribute__((packed)) dhcpack_t;

static char *name;
static int sockfd;
static uint8_t hwaddr[DHCPHDR_HLEN_ETH];

static void usage() {
	fprintf(stderr, "%s: usage: %s device\n", name, name);
	fprintf(stderr, "%s: example: %s vionet0\n", name, name);
	exit(EXIT_FAILURE);
}

static void logstrerror(char *msg) {
	fprintf(stderr, "%s: %s: %s\n", name, msg, strerror(errno));
}

static void logmsg(char *msg) {
	printf("%s: %s\n", name, msg);
}

static void logerr(char *msg) {
	fprintf(stderr, "%s: %s\n", name, msg);
}

int createsocket(char *device) {
	// initialize the DHCP socket and bind it to the client port
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0) {
		logstrerror("failed to create socket");
		return 1;
	}

	struct sockaddr_in addr;
	addr.sin_port = htons(DHCP_CLIENT_PORT);
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(sockfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in))) {
		logstrerror("failed to bind socket");
		return 1;
	}

	// make the socket able to broadcast datagrams and bind it to the chosen device
	int broadcast = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast))) {
		logstrerror("failed to make socket accept broadcasts");
		return 1;
	}

	if (setsockopt(sockfd, SOL_SOCKET, SO_BINDTODEVICE, device, strlen(device) + 1)) {
		logstrerror("failed to bind to device");
		return 1;
	}

	return 0;
}

int getdevicehwaddr(char *device) {
	// get hardware address of device
	struct ifreq buffer = {0};
	strcpy(buffer.ifr_name, device);
	if (ioctl(sockfd, SIOCGIFHWADDR, &buffer)) {
		logstrerror("failed to get device hardware address");
		return 1;
	}

	memcpy(hwaddr, buffer.ifr_hwaddr.sa_data, DHCPHDR_HLEN_ETH);

	char printbuff[50];
	snprintf(printbuff, 50, "hardware address: %02x:%02x:%02x:%02x:%02x:%02x", hwaddr[0], hwaddr[1], hwaddr[2], hwaddr[3], hwaddr[4], hwaddr[5]);
	logmsg(printbuff);
	return 0;
}

static int discover() {
	// send out the DHCPDISCOVER packet
	dhcpdiscover_t packet;
	memset(&packet, 0, sizeof(packet));

	packet.hdr.op = DHCPHDR_TYPE_CLIENT;
	packet.hdr.htype = DHCPHDR_HTYPE_ETH;
	packet.hdr.hlen = DHCPHDR_HLEN_ETH;
	packet.hdr.xid = htonl(DHCPHDR_XID);
	memcpy(packet.hdr.clienthw, hwaddr, DHCPHDR_HLEN_ETH);
	packet.hdr.cookie = htonl(DHCPHDR_COOKIE);

	memcpy(packet.opts, discoveropts, sizeof(packet.opts));

	// broadcast address
	struct sockaddr_in address = {
		.sin_family = AF_INET,
		.sin_port = htons(DHCP_SERVER_PORT),
		.sin_addr.s_addr = htonl(INADDR_BROADCAST)
	};

	if (sendto(sockfd, &packet, sizeof(packet), 0, (struct sockaddr *)&address, sizeof(address)) < 0) {
		logstrerror("sending DHCPDISCOVER failed");
		return 1;
	}

	return 0;
}

static int waitforserver(int timeout) {
	struct pollfd pollfd = {
		.fd = sockfd,
		.events = POLLIN
	};

	int pollr = poll(&pollfd, 1, timeout);
	return pollr <= 0 ? 1 : 0;
}

static inline uint32_t optget32(uint8_t *opt, int i) {
	return ((uint32_t)opt[i] << 24) | ((uint32_t)opt[i + 1] << 16) | ((uint32_t)opt[i + 2] << 8) | ((uint32_t)opt[i + 3]);
}


typedef struct {
	uint32_t clientip;
	uint32_t serverip;
	uint32_t netmask;
	uint32_t router;
	uint32_t leasetime;
} offer_t;

static int processoffer(offer_t *offer, uint32_t *xid) {
	uint8_t buffer[1024];
	dhcpoffer_t *dhcpoffer = (dhcpoffer_t *)buffer;
	memset(buffer, 0, sizeof(buffer));

	size_t nbytes = recv(sockfd, buffer, sizeof(buffer), 0);
	if (nbytes < 0) {
		logstrerror("failed to recv while processing offer");
		return 1;
	}

	// not from the server, ignore
	if (dhcpoffer->hdr.op != DHCPHDR_TYPE_SERVER) {
		*xid = 0;
		return 0;
	}

	size_t optsize = nbytes - sizeof(dhcphdr_t);
	// get the offered stuff in the options
	int i = 0;
	uint8_t *opts = dhcpoffer->opts;
	while (i < optsize) {
		switch (opts[i]) {
			case OPTION_TYPE:
				if (opts[i + 2] != OPTION_TYPE_DHCPOFFER) {
					// not a DHCPOFFER, ignore
					*xid = 0;
					return 0;
				}
				break;
			case OPTION_MASK:
				uint32_t mask = optget32(opts, i + 2);
				offer->netmask = mask;
				break;
			case OPTION_ROUTER :
				uint32_t router = optget32(opts, i + 2);
				offer->router = router;
				break;
			case OPTION_SERVER:
				uint32_t server = optget32(opts, i + 2);
				offer->serverip = server;
				break;
			case OPTION_LEASETIME:
				uint32_t leasetime = optget32(opts, i + 2);
				offer->leasetime = leasetime;
				break;
			case OPTION_END:
				goto leave;
		}

		uint8_t optlen = opts[i + 1];
		i += optlen + 2;
	}
	leave:

	offer->clientip = ntohl(dhcpoffer->hdr.responseip);

	*xid = ntohl(dhcpoffer->hdr.xid);
	return 0;
}

static int request(offer_t *offer) {
	// send out the DHCPREQUEST packet
	dhcprequest_t packet;
	memset(&packet, 0, sizeof(packet));

	packet.hdr.op = DHCPHDR_TYPE_CLIENT;
	packet.hdr.htype = DHCPHDR_HTYPE_ETH;
	packet.hdr.hlen = DHCPHDR_HLEN_ETH;
	packet.hdr.xid = htonl(DHCPHDR_XID);
	packet.hdr.clientip = htonl(offer->clientip);
	packet.hdr.serverip = htonl(offer->serverip);
	memcpy(packet.hdr.clienthw, hwaddr, DHCPHDR_HLEN_ETH);
	packet.hdr.cookie = htonl(DHCPHDR_COOKIE);

	packet.opts.type[0] = OPTION_TYPE;
	packet.opts.type[1] = OPTION_TYPE_LENGTH;
	packet.opts.type[2] = OPTION_TYPE_DHCPREQUEST;
	packet.opts.ipreqhdr[0] = OPTION_IPREQ;
	packet.opts.ipreqhdr[1] = 4;
	packet.opts.ipreq = htonl(offer->clientip);
	packet.opts.dhcpserverhdr[0] = OPTION_SERVER;
	packet.opts.dhcpserverhdr[1] = 4;
	packet.opts.dhcpserver = htonl(offer->serverip);
	packet.opts.end = 0xff;

	// broadcast address
	struct sockaddr_in address = {
		.sin_family = AF_INET,
		.sin_port = htons(DHCP_SERVER_PORT),
		.sin_addr.s_addr = htonl(INADDR_BROADCAST)
	};

	if (sendto(sockfd, &packet, sizeof(packet), 0, (struct sockaddr *)&address, sizeof(address)) < 0) {
		logstrerror("sending DHCPREQUEST failed");
		return 1;
	}

	return 0;
}

static int getack(uint32_t *xid) {
	uint8_t buffer[1024];
	dhcpoffer_t *dhcpack = (dhcpoffer_t *)buffer;
	memset(buffer, 0, sizeof(buffer));

	size_t nbytes = recv(sockfd, buffer, sizeof(buffer), 0);
	if (nbytes < 0) {
		logstrerror("failed to recv while processing ack");
		return 1;
	}

	// not from the server, ignore
	if (dhcpack->hdr.op != DHCPHDR_TYPE_SERVER) {
		*xid = 0;
		return 0;
	}

	size_t optsize = nbytes - sizeof(dhcphdr_t);
	// get the offered stuff in the options
	int i = 0;
	uint8_t *opts = dhcpack->opts;
	while (i < optsize) {
		switch (opts[i]) {
			case OPTION_TYPE:
				if (opts[i + 2] == OPTION_TYPE_DHCPACK) {
					*xid = ntohl(dhcpack->hdr.xid);
					return 0;
				}
				break;
			case OPTION_END:
				goto leave;
		}

		uint8_t optlen = opts[i + 1];
		i += optlen + 2;
	}
	leave:

	// if we reach this, its not an ack packet.
	*xid = 0;
	return 0;
}

static void printip(char *ipname, uint32_t ip) {
	struct in_addr addr;
	addr.s_addr = htonl(ip);
	char *ipstr = inet_ntoa(addr);
	printf("%s: %s: %s\n", name, ipname, ipstr);
}

static void printoffer(offer_t *offer) {
	printip("client ip", offer->clientip);
	printip("server ip", offer->serverip);
	printip("netmask", offer->netmask);
	printip("router", offer->router);
	printf("%s: lease time: %d seconds\n", name, offer->leasetime);
}

static void addroute(char *device, uint32_t addr, uint32_t gateway, uint32_t mask, int weight) {
	struct rtentry route;
	memset(&route, 0, sizeof(route));

	struct sockaddr_in sockaddr;
	struct sockaddr_in sockgateway;
	struct sockaddr_in sockmask;

	sockaddr.sin_family = AF_INET;
	sockaddr.sin_addr.s_addr = htonl(addr);
	sockgateway.sin_family = AF_INET;
	sockgateway.sin_addr.s_addr = htonl(gateway);
	sockmask.sin_family = AF_INET;
	sockmask.sin_addr.s_addr = htonl(mask);

	memcpy(&route.rt_dst, &sockaddr, sizeof(struct sockaddr_in));
	memcpy(&route.rt_gateway, &sockgateway, sizeof(struct sockaddr_in));
	memcpy(&route.rt_genmask, &sockmask, sizeof(struct sockaddr_in));
	route.rt_metric = weight;
	route.rt_dev = device;

	if (ioctl(sockfd, SIOCADDRT, &route) < 0)
		logstrerror("failed to add route");
}

int main(int argc, char *argv[]) {
	name = argv[0];

	if (argc != 2)
		usage();

	if (createsocket(argv[1]))
		return EXIT_FAILURE;

	if (getdevicehwaddr(argv[1]))
		return EXIT_FAILURE;

	offer_t offer;

	bool gotoffer = false;

	while (gotoffer == false) {
		if (discover())
			return EXIT_FAILURE;

		while (gotoffer == false) {
			if (waitforserver(10000))
				break;

			uint32_t xid;

			if (processoffer(&offer, &xid))
				return EXIT_FAILURE;

			// packet wasn't for this machine
			if (xid != DHCPHDR_XID)
				continue;

			gotoffer = true;
		}
	}

	bool gotack = false;
	while (gotack == false) {
		if (request(&offer))
			return EXIT_FAILURE;

		while (gotack == false) {
			if (waitforserver(10000))
				break;

			uint32_t xid;

			if (getack(&xid))
				return EXIT_FAILURE;

			if (xid != DHCPHDR_XID)
				continue;

			gotack = true;
		}
	}

	// TODO routing table information
	printoffer(&offer);

	addroute(argv[1], 0, offer.router, 0, 1);
	addroute(argv[1], offer.clientip, 0, offer.netmask, 10);

	return EXIT_SUCCESS;
}
