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

#include <kernel/types.h>

#include <utils/compiler.h>

#include <arch.h>

struct packet;

/** Version field inside the IP header */
#define IPV4_VERSION 4
/** Minimum size of an IP header */
#define IPV4_MIN_LENGTH 20

/** An IPv4 header
 *  @note All fields are in big endian
 */
struct PACKED ALIGNED(sizeof(uint16_t)) ipv4_header {
    uint8_t version : 4;
    uint8_t ihl : 4;
    uint8_t tos;
    __be uint16_t tot_len;
    __be uint16_t id;
    __be uint16_t frag_off;
    uint8_t ttl;
    uint8_t protocol;
    __be uint16_t check;
    __be ipv4_t saddr;
    __be ipv4_t daddr;
};

static_assert(sizeof(struct ipv4_header) == IPV4_MIN_LENGTH);

/** Values for the protocol field inside the IPv4 header
 *  @enum ipv4_protocol
 */
enum ipv4_protocol {
    IP_PROTOCOL_TCP = 6,  /** TCP */
    IP_PROTOCOL_UDP = 17, /** UDP */
};

/** Insert headers for layer 3 and below into an IP packet */
void ipv4_fill_packet(struct packet *, struct ipv4_header *);

/** Helper to quickly generate an IPv4 address */
static inline ipv4_t IPV4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return a << 24 | b << 16 | c << 8 | d;
}

#define FMT_IP "%u.%u.%u.%u"
#define LOG_IP(ip)                                                 \
    ((uint8_t *)&ip)[0], ((uint8_t *)&ip)[1], ((uint8_t *)&ip)[2], \
        ((uint8_t *)&ip)[3]

#endif /* KERNEL_NET_IPV4_H */

/** @} */
