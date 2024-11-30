/**
 * @file kernel/net/ethernet.h
 *
 * @defgroup networking_ethernet Layer 2 - Ethernet
 * @ingroup networking
 *
 * @see - TCP/IP Illustrated Vol I - Chapter 2 - Link Layer
 * @see - https://ieeexplore.ieee.org/document/9844436
 *
 * @{
 */

#ifndef KERNEL_NET_ETHERNET_H
#define KERNEL_NET_ETHERNET_H

#include <kernel/error.h>
#include <kernel/types.h>

#include <utils/compiler.h>

/** Size of an ethernet MAC address */
#define ETHERNET_ADDR_SIZE 6
/** Size of an ethernet header */
#define ETHERNET_HEADER_SIZE 14

/** Represents an ethernet MAC address.
 *  @note The content of this buffer is **ALWAYS** in big endian notation.
 */
typedef uint8_t mac_address_t[ETHERNET_ADDR_SIZE];

struct packet;

/** An ethernet frame */
struct PACKED ethernet_header {
    mac_address_t dst;      ///< Receiver's ethernet MAC address
    mac_address_t src;      ///< Sender's ethernet MAC address
    __be uint16_t protocol; ///< The L3 protocol of the payload's packet
};

static_assert(sizeof(struct ethernet_header) == ETHERNET_HEADER_SIZE);

/** Supported ethernet protocols, as defined by IANA.
 * @see https://www.iana.org/assignments/ieee-802-numbers/ieee-802-numbers.xhtml
 */
enum ethernet_type {
    ETH_PROTO_IP = 0x0800,  ///< IPv4
    ETH_PROTO_ARP = 0x0806, ///< Address resolution protocol
};

/** Insert headers for layer 2 inside a packet over ethernet */
void ethernet_fill_packet(struct packet *, enum ethernet_type,
                          const mac_address_t dst);

/** Process a packet received by an ethernet network device */
error_t ethernet_receive_packet(struct packet *);

static inline void ethernet_fill_mac(mac_address_t mac, uint64_t mac_raw)
{
    mac[5] = mac_raw;
    mac[4] = mac_raw >> 8;
    mac[3] = mac_raw >> 16;
    mac[2] = mac_raw >> 24;
    mac[1] = mac_raw >> 32;
    mac[0] = mac_raw >> 40;
}

#define LOG_MAC_ARG(_mac) mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]
#define LOG_FMT_MAC "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx"

#endif /* KERNEL_NET_ETHERNET_H */

/** @} */
