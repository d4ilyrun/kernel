/**
 * @defgroup networking_packet Packet
 * @ingroup networking
 *
 * # Network Packet
 *
 * The network packet is the core of the network API.
 * Each time we send or receive a packet, a new \ref packet is created to
 * hold its data and be passed to the API's function.
 *
 * This packet's design is inspired by Linux's socket buffer interface.
 * Even though this is a heavily simplified version of it, the philosophy
 * remains the same:
 *
 * - The packet's content is hidden after its metadata struct
 * - changes to the packet can only be done incrementally (similar to a stack)
 *
 * @{
 */

#ifndef KERNEL_NET_PACKET_H
#define KERNEL_NET_PACKET_H

#include <kernel/error.h>
#include <kernel/types.h>

#include <libalgo/queue.h>
#include <utils/math.h>

struct ethernet_device;
/* L2 headers */
struct ethernet_header;
/* L3 headers */
struct ipv4_header;
struct arp_header;

/** A network packet.
 *
 * @struct packet
 *
 * The packet structure does not contain the packet directly, it just holds
 * metadata and information about it and its usage. Instead, the packet
 * is located right after it, in a pre-allocated memory buffer, whose size
 * is specified by the caller during the struct's allocation.
 *
 * The user should **NEVER** interact with the packet's buffer directly, and
 * should always use the \c packet_put_data function to add new data to the
 * packet. This way, adding data to the packet can only be done incrementally,
 * layer after layer, which corresponds to the network API's philosophy.
 */
struct packet {
    size_t allocated_size; ///< Size allocated for the packet
    size_t packet_size;    ///< Size of the contained packet
    size_t popped;         ///< Number of popped bytes

    /** The internet device this packet went/is going through */
    struct ethernet_device *netdev;

    /** The packet's link layer header */
    union {
        void *raw;
        struct ethernet_header *ethernet;
    } l2;

    /** The packet's network layer header */
    union {
        void *raw;
        struct ipv4_header *ipv4;
        struct arp_header *arp;
    } l3;

    /** Start of the packet's content (L4 and beyond) */
    void *payload;

    node_t rx_this; /*!< Used by socket to list received packets */
};

#define PACKET_ALIGN (sizeof(uint64_t))

/** Create a new packet */
struct packet *packet_new(size_t packet_size);

/** Clone a packet.
 *
 *  The packet's content as well as the current offsets will be duplicated,
 *  so that one can use a duplicated packet just like the original one.
 */
struct packet *packet_clone(const struct packet *packet);

/** Free a packet */
void packet_free(struct packet *packet);

/** Send the packet */
error_t packet_send(struct packet *packet);

/** Find the start of the actual packet's content */
static inline void *packet_start(const struct packet *packet)
{
    return ((void *)packet) + align_up(sizeof(struct packet), PACKET_ALIGN);
}

/** @return The current number of bytes placed into the packet */
static inline size_t packet_size(const struct packet *packet)
{
    return packet->packet_size;
}

/** @return The current number of bytes that can be read from the packet */
static inline size_t packet_read_size(const struct packet *packet)
{
    return packet->packet_size - packet->popped;
}

/** Find the end of the actual packet's content */
static inline void *packet_end(const struct packet *packet)
{
    return packet_start(packet) + packet_size(packet);
}

/** Append new data to the packet */
error_t packet_put(struct packet *packet, const void *data, size_t size);

/** Read data from the packet.
 *  @return The number of bytes actually read.
 */
size_t packet_peek(struct packet *packet, void *data, size_t size);

/** Read data from the packet.
 *  Popped data cannot be re-read afterwards.
 *  @return The number of bytes actually read.
 */
size_t packet_pop(struct packet *packet, void *data, size_t size);

/** Append new data of literal type to the packet (e.g. integers) */
#define packet_put_literal(packet, data) packet_put(packet, &data, sizeof(data))

/** Read literal from the packet (e.g. integers) */
#define packet_peek_literal(packet, data) \
    packet_peek(packet, &data, sizeof(data))

/** Pop literal from the packet (e.g. integers) */
#define packet_pop_literal(packet, data) packet_pop(packet, &data, sizeof(data))

/** Set the current end of the packet as the start of the link layer */
static inline void packet_mark_l2_start(struct packet *packet)
{
    packet->l2.raw = packet_end(packet);
}

/** Set the start of the network layer N bytes after the link layer */
static inline void packet_set_l2_size(struct packet *packet, size_t size)
{
    packet->l3.raw = packet->l2.raw + size;
}

/** Set the current end of the packet as the start of the network layer */
static inline void packet_mark_l3_start(struct packet *packet)
{
    packet->l3.raw = packet_end(packet);
}

/** Set the start of the packet payload N bytes after the network layer */
static inline void packet_set_l3_size(struct packet *packet, size_t size)
{
    packet->payload = packet->l3.raw + size;
}

/** @return the sart of the packet's payload (L4) */
static inline void *packet_payload(const struct packet *packet)
{
    return packet->payload;
}

/** @return the size of the packet's header */
static inline size_t packet_header_size(const struct packet *packet)
{
    return packet->payload -
           (((void *)packet) + align_up(sizeof(struct packet), PACKET_ALIGN));
}

/** @return the size of the packet's payload */
static inline size_t packet_payload_size(const struct packet *packet)
{
    return packet_end(packet) - packet->payload;
}

#endif /* KERNEL_NET_PACKET_H */

/** @} */
