/**
 * @file kernel/net.h
 *
 * @defgroup networking Networking
 * @ingroup kernel
 *
 * # Networking
 *
 * This is the core of the kernel's networking API.
 *
 * The goal for this API is to follow the OSI model's philosophy
 * and organize things in a hierarchical manner.
 *
 * There should be one sub-api for each "protocol" of each layer.
 * These functions should only interact with **THEIR** part of the packet,
 * and call the lower/higher layer API's function to process the next
 * part of the packet.
 *
 * For example, when sending a TCP packet over ethernet, the codepath
 * should look like so:
 *
 * @code
 * packet = packet_new()                // create a new packet
 * tcp_packet_fill(packet)              // responsible for adding L4 info
 *   -> ipv4_packet_fill(packet)        // same for L3
 *     -> ethernet_packet_fill(packet)  // same for L2
 * packet_send(packet)
 * @endcode
 *
 * @warning All fields and values inside the network API's structures are in
 * big endian (the standard network endianness). Do not forget to use the
 * \ref ntoh and \ref hton functions when comparing with variables that use
 * the architecture's natural alignment.
 *
 * ## Important structures/concepts
 *
 * * @ref networking_packet
 * * @ref kernel_device_netdev
 * * @ref networking_sockets
 *
 * ## TODO: Networking
 *
 * - Workqueue based packet processing (non-blocking)
 * - Transport layer (TCP/UDP)
 *
 * @{
 */

#ifndef KERNEL_NET_H
#define KERNEL_NET_H

#include <uapi/kernel/net.h>

#include <utils/bits.h>

struct sockaddr_mac {
    sa_family_t mac_family; /* AF_UNSPEC */
    uint8_t mac_addr[6];
};

#define htons htobe16
#define ntohs be16toh
#define htonl htobe32
#define ntohl be32toh

/** Convert an interger to its network representation (big endian) */
#define hton(_x)                     \
    _Generic((_x), uint16_t          \
             : htons((_x)), uint32_t \
             : htonl((_x)), default  \
             : (_x))

/** Convert an interger from its network representation to that of the host */
#define ntoh(_x)                     \
    _Generic((_x), uint16_t          \
             : ntohs((_x)), uint32_t \
             : ntohl((_x)), default  \
             : (_x))

#endif /* KERNEL_NET_H */

/** @} */
