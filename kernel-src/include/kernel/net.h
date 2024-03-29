#ifndef _NET_H
#define _NET_H

#include <stddef.h>
#include <stdint.h>

#include <kernel/eth.h>

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
	int (*allocdesc)(size_t requestedsize, netdesc_t *desc);
	int (*sendpacket)(struct netdev_t *_internal, netdesc_t desc, mac_t targetmac, int proto);
} netdev_t;

typedef struct {
	uint16_t hwtype;
	uint16_t protocoltype;
	uint8_t hwlen;
	uint8_t ptlen;
	uint16_t opcode;
	uint8_t srchw[6];
	uint8_t srcpt[4];
	uint8_t dsthw[6];
	uint8_t dstpt[4];
} __attribute__((packed)) arpframe_t;

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

#define NET_BROADCAST_MAC (mac_t){.address = {0xff,0xff,0xff,0xff,0xff,0xff}}

#define IPV4_PROTO_UDP 0x11

int udp_allocdesc(netdev_t *netdev, size_t requestedsize, netdesc_t *desc);
int udp_sendpacket(netdev_t *netdev, netdesc_t desc, uint32_t ip, uint16_t srcport, uint16_t dstport);
int ipv4_allocdesc(netdev_t *netdev, size_t requestedsize, netdesc_t *desc);
int ipv4_sendpacket(netdev_t *netdev, netdesc_t desc, uint32_t ip, int proto);

#endif
