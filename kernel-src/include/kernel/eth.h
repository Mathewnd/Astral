#ifndef _ETH_H
#define _ETH_H

#include <stdint.h>

typedef struct {
	uint8_t destination[6];
	uint8_t source[6];
	uint16_t type;
} __attribute__((packed)) ethframe_t;

#define ETH_PROTO_IP 0x0800
#define ETH_PROTO_ARP 0x0806

#endif
