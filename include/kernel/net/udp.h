#ifndef _KERNEL_NET_UDP_H
#define _KERNEL_NET_UDP_H

#include <kernel/error.h>
#include <kernel/net/packet.h>
#include <kernel/types.h>

#include <utils/compiler.h>

extern struct socket_protocol_ops af_inet_udp_ops;

struct PACKED udp_header {
	__be u16 sport;  /* source port */
	__be u16 dport;  /* destination port */
	__be u16 length; /* datagram length, including header */
	__be u16 checksum;
};

error_t udp_receive_packet(struct packet *packet);

#endif /* _KERNEL_NET_UDP_H */
