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
#include <kernel/types.h>

#include <utils/compiler.h>

#include <arch.h>

struct packet;
struct net_route;

/** Version field inside the IP header */
#define IPV4_VERSION 4
/** Minimum size of an IP header */
#define IPV4_MIN_LENGTH 20
/** TTL value used when creating packets */
#define IPV4_DEFAULT_TTL 64

/** An IPv4 header
 *  @note All fields are in big endian
 */
struct PACKED ALIGNED(sizeof(uint16_t)) ipv4_header {
#if defined(ARCH_LITTLE_ENDIAN)
    uint8_t ihl : 4;
    uint8_t version : 4;
#else
    uint8_t version : 4;
    uint8_t ihl : 4;
#endif
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

#define IPV4_FRAG_MASK 0x1FFF
#define IPV4_RESERVED (0x4 << 13)
#define IPV4_NOFRAG (0x2 << 13)
#define IPV4_MORE_FRAG (0x1 << 13)

/** @return An IPv4 fragment's offset */
static inline uint16_t ipv4_fragment_offset(const struct ipv4_header *iphdr)
{
    return ntohs(iphdr->frag_off) & IPV4_FRAG_MASK;
}

/** @return Whether this header's packet has more fragments remaining */
static inline bool ipv4_more_framents(const struct ipv4_header *iphdr)
{
    return ntohs(iphdr->frag_off) & IPV4_MORE_FRAG;
}

/** @return Whether this header's packet is fragmented */
static inline bool ipv4_is_fragmented(const struct ipv4_header *iphdr)
{
    return ntohs(iphdr->frag_off) & (IPV4_MORE_FRAG | IPV4_FRAG_MASK);
}

/***/
static inline bool ipv4_is_multicast(__be ipv4_t addr)
{
    return (ntohl(addr) >> 28) == 0xE;
}

/***/
static inline bool ipv4_is_broadcast(__be ipv4_t addr)
{
    return addr == 0XFFFFFFFF;
}

/** Process a newly received IP packet */
error_t ipv4_receive_packet(struct packet *packet);

/** Build an IP packet
 *  The L2/L3 headers are filled using the routing information.
 */
struct packet *ipv4_build_packet(const struct net_route *, u8 protocol,
                                 const void *payload, size_t);

/** Helper to quickly generate an IPv4 address */
static inline __be ipv4_t IPV4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return htonl(a << 24 | b << 16 | c << 8 | d);
}

#define FMT_IP "%u.%u.%u.%u"
#define LOG_IP(ip)                                                 \
    ((uint8_t *)&ip)[0], ((uint8_t *)&ip)[1], ((uint8_t *)&ip)[2], \
        ((uint8_t *)&ip)[3]

#endif /* KERNEL_NET_IPV4_H */

/** @} */
