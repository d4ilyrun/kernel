#ifndef _DAILYRUN_NET_IPV4_H
#define _DAILYRUN_NET_IPV4_H

#include <arpa/inet.h>
#include <stdbool.h>

/*
 * IPv4 header format.
 *
 * NOTE: All fields are in network byte order (big endian).
 */
struct __attribute__((packed)) ipv4_header {
#ifdef __LITTLE_ENDIAN__
	uint8_t ihl : 4;
	uint8_t version : 4;
#else
	uint8_t version : 4;
	uint8_t ihl : 4;
#endif
	uint8_t tos;
	uint16_t tot_len;
	uint16_t id;
	uint16_t frag_off;
	uint8_t ttl;
	uint8_t protocol;
	uint16_t check;
	uint32_t saddr;
	uint32_t daddr;
};

/** Version field inside the IPv4 header */
#define IPV4_VERSION 4
/** Minimum size of an IP header */
#define IPV4_MIN_LENGTH 20
/** TTL value used when creating packets */
#define IPV4_DEFAULT_TTL 64

static inline size_t ipv4_header_size(const struct ipv4_header *iphdr)
{
	return iphdr->ihl * sizeof(uint32_t);
}

static inline size_t ipv4_header_option_size(const struct ipv4_header *iphdr)
{
	return ipv4_header_size(iphdr) - sizeof(*iphdr);
}

/*
 * Bitmasks used to parse the IPv4 header's frag_off field .
 */
#define IPV4_FRAG_MASK 0x1FFF
#define IPV4_RESERVED  (0x4 << 13)
#define IPV4_NOFRAG    (0x2 << 13)
#define IPV4_MORE_FRAG (0x1 << 13)

static inline uint16_t ipv4_fragment_offset(const struct ipv4_header *iphdr)
{
	return ntohs(iphdr->frag_off) & IPV4_FRAG_MASK;
}

static inline bool ipv4_more_framents(const struct ipv4_header *iphdr)
{
	return ntohs(iphdr->frag_off) & IPV4_MORE_FRAG;
}

static inline bool ipv4_is_fragmented(const struct ipv4_header *iphdr)
{
	return ntohs(iphdr->frag_off) & (IPV4_MORE_FRAG | IPV4_FRAG_MASK);
}

/*
 * Pseudo IPv4 header prefixed to UDP and TCP packets during checksum computation.
 *
 * NOTE: All fields are in network byte order (big endian).
 */
struct __attribute__((packed)) pseudo_ipv4_header {
	uint32_t saddr;
	uint32_t daddr;
	uint8_t zero;
	uint8_t proto;
	uint16_t proto_len;
};

static inline bool ipv4_is_multicast(in_addr_t addr)
{
	return (ntohl(addr) >> 28) == 0xE;
}

static inline bool ipv4_is_broadcast(in_addr_t addr)
{
	return addr == INADDR_BROADCAST;
}

#endif /* _DAILYRUN_NET_IPV4_H */
