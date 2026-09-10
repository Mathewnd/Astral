#ifndef _RAW_H
#define _RAW_H

void raw_process(netdev_t *netdev, void *buffer, uint32_t peer);
socket_t *raw_create_socket(void);

#endif
