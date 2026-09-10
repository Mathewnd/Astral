#ifndef _RAW_H
#define _RAW_H

typedef struct socket socket_t;

typedef struct {
	uint16_t netdev; // 1-based index that gets transformed into a minor number
	uint16_t proto;
	uint8_t mac[6];
} raw_addr_t;

void raw_process(netdev_t *netdev, void *buffer, mac_t *sender, uint16_t proto, size_t size);
socket_t *raw_create_socket(int protocol);

#endif
