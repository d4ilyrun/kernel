/**
 * @file kernel/net/arp.h
 *
 * @defgroup networking_arp Layer 3 - Address Resolution Protocol (ARP)
 * @ingroup networking
 *
 * @ref TCP/IP Illustrated Vol I - Chapter 4 - ARP
 * @ref RFC 826
 *
 * @{
 */

#ifndef KERNEL_NET_ARP_H
#define KERNEL_NET_ARP_H

#include <kernel/net/ethernet.h>
#include <kernel/net/ipv4.h>

/** The maximum total size of an ARP packet */
#define ARP_PACKET_SIZE \
    (sizeof(struct ethernet_header) + sizeof(struct arp_header))

/** An ARP header.
 *
 * @note An ARP header can theoretically contain multiple types of hw/proto
 * address combinations. However, we currently only support IP over Ethernet.
 */
struct PACKED arp_header {
    __be uint16_t hw_type;   ///< Hardware Address type
    __be uint16_t prot_type; ///< Protocol address type
    uint8_t hw_length;       ///< Hardware address length
    uint8_t prot_length;     ///< Protocol address length
    __be uint16_t operation; ///< @see enum arp_operation
    mac_address_t src_mac;   ///< Sender's hardware address
    __be ipv4_t src_ip;      ///< Sender's protocol address
    mac_address_t dst_mac;   ///< Recipient's hardware address (or broadcast)
    __be ipv4_t dst_ip;      ///< Recipient's protocol address
};

/** The different accepted types of hardware addresses */
enum arp_hw_type {
    ARP_HW_ETHERNET = 1, ///< Ethernet MAC address
};

/** The different ARP operations */
enum arp_operation {
    ARP_REQUEST = 1, ///< Request. Sent first to try and find a MAC address.
    ARP_REPLY = 2,   ///< Reply. Sent back to the sender if a match was found.
};

/** Compute the size of an ARP header.
 *
 *  The ARP protocol header size is of variable length,
 *  always use this function instead of the usual \c sizeof(struct arp_header).
 */
static inline size_t arp_header_size(struct arp_header *hdr)
{
    return sizeof(*hdr);
}

/** Add a new entry inside the ARP table.
 *  @param ip The IP address (in **network** order)
 *  @param mac The MAC address associated with the IP
 */
 error_t arp_add(__be ipv4_t, mac_address_t);

/** Retreive the MAC address associated with an IPv4 address.
 *  @param ip The IP address (in **network** order)
 *  @return The MAC address, or NULL if the IP isn't inside the table
 */
const mac_address_t *arp_get(__be ipv4_t);

/** Handle a received ARP packet */
error_t arp_receive_packet(struct packet *);

/** Send an ARP request/reply */
error_t arp_send_packet(struct arp_header *);

#endif /* KERNEL_NET_ARP_H */

/** @} */
