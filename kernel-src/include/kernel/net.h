#ifndef _NET_H
#define _NET_H

#include <stddef.h>
#include <stdint.h>

#include <hashtable.h>
#include <mutex.h>
#include <event.h>
#include <string.h>
#include <arch/cpu.h>
#include <kernel/abi.h>
#include <kernel/alloc.h>
#include <errno.h>
#include <logging.h>

typedef struct {
	void *address;
	uintmax_t curroffset;
	size_t size;
} netdesc_t;

typedef struct {
	uint8_t address[6];
} __attribute__((packed)) mac_t;

typedef struct netdev_t {
	mac_t mac;
	size_t mtu;
	uint32_t ip;
	hashtable_t arpcache;
	bool doarp;
	int (*allocdesc)(size_t requestedsize, netdesc_t *desc);
	int (*sendpacket)(struct netdev_t *_internal, netdesc_t desc, mac_t targetmac, int proto);
} netdev_t;

typedef struct {
	uint8_t version_length;
	uint8_t servicetype_ecn;
	uint16_t packetlen;
	uint16_t id;
	uint16_t flags_fragoffset;
	uint8_t timetolive;
	uint8_t protocol;
	uint16_t framechecksum;
	uint32_t srcaddr;
	uint32_t dstaddr;
} __attribute__((packed)) ipv4frame_t;

typedef struct {
	uint16_t srcport;
	uint16_t dstport;
	uint16_t length;
	uint16_t checksum;
} __attribute__((packed)) udpframe_t;

typedef struct {
	uint16_t port;
	uint32_t addr;
} ipv4addr_t;

typedef struct {
	union {
		ipv4addr_t ipv4addr;
		char unaddr[256];
	};
} sockaddr_t;

#define NET_BROADCAST_MAC (mac_t){.address = {0xff,0xff,0xff,0xff,0xff,0xff}}

#define MAC_EQUAL(m1,m2) (memcmp(m1, m2, sizeof(mac_t)) == 0)

#define IPV4_PROTO_UDP 0x11
#define IPV4_BROADCAST_ADDRESS 0xffffffff

void arp_init();
void ipv4_init();
void udp_init();
void netdev_init();
void loopback_init();
netdev_t *loopback_device();
void udp_process(netdev_t *netdev, void *buffer, uint32_t ip);
void arp_process(netdev_t *netdev, void *buffer);
int arp_lookup(netdev_t *netdev, uint32_t ip, mac_t *mac);
int udp_sendpacket(void *buffer, size_t packetsize, uint32_t ip, uint16_t srcport, uint16_t dstport, netdev_t *broadcastdev);
int ipv4_sendpacket(void *buffer, size_t packetsize, uint32_t ip, int proto, netdev_t *broadcastdev);
void ipv4_process(netdev_t *netdev, void *nextbuff);
int ipv4_addroute(netdev_t *netdev, uint32_t addr, uint32_t gateway, uint32_t mask, int weight);
int netdev_register(netdev_t *netdev, char *name);
netdev_t *netdev_getdev(char *name);

#endif
