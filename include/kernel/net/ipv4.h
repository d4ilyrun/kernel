/**
 * @brief IPv4 - Internet Protocol
 * @file kernel/net/ipv4.h
 *
 * @defgroup networking_ipv4 Layer 3 - Internet Protocol (IPv4)
 * @ingroup networking
 *
 * @ref TCP/IP Illustrated Vol I - Chapter 5 - IP
 * @ref RFC 791
 *
 * @{
 */

#ifndef KERNEL_NET_IPV4_H
#define KERNEL_NET_IPV4_H

#include <kernel/error.h>
#include <kernel/net.h>
#include <kernel/net/route.h>
#include <kernel/types.h>

#include <dailyrun/net/ipv4.h>

#include <utils/compiler.h>

#include <arch.h>

struct packet;

/** Process a newly received IP packet */
error_t ipv4_receive_packet(struct packet *packet);

/** Build an IP packet
 *  The L2/L3 headers are filled using the routing information.
 */
struct packet *ipv4_build_packet(const struct net_route *route, u8 proto, const void *header,
				 size_t header_size,			    /* L4 header */
				 const void *payload, size_t payload_size); /* Payload */

/** Helper to quickly generate an IPv4 address */
static inline __be ipv4_t IPV4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
	return htonl(a << 24 | b << 16 | c << 8 | d);
}

/*
 * Helper functions for socket protocols over IPv4.
 */

struct inet_sock {
	struct net_route route;
	/* local address (obtained via bind()) */
	__be u32 addr;
	__be u16 port;
};

error_t inet_sock_init(struct inet_sock *isock);
error_t inet_sock_bind(struct inet_sock *isock, const struct sockaddr_in *sin);
error_t inet_sock_connect(struct inet_sock *isock, const struct sockaddr_in *sin);
ssize_t inet_sock_send_one(struct inet_sock *isock, __be u16 proto,
			   const void *header, size_t header_size,
			   const struct iovec *iov, int flags);

#endif /* KERNEL_NET_IPV4_H */

/** @} */
