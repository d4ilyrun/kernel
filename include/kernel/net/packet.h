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

#include <utils/math.h>

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

    /** The packet's link layer header */
    union {
        void *raw;
    } l2;

    /** The packet's network layer header */
    union {
        void *raw;
    } l3;
};

#define PACKET_ALIGN (sizeof(uint64_t))

/** Create a new packet */
struct packet *packet_new(size_t packet_size);

/** Send the packet */
error_t packet_send(struct packet *packet);

/** Find the start of the actual packet's content */
static inline void *packet_start(struct packet *packet)
{
    return ((void *)packet) + align_up(sizeof(struct packet), PACKET_ALIGN);
}

/** @return The current number of bytes placed into the packet */
static inline size_t packet_size(struct packet *packet)
{
    return packet->packet_size;
}

/** Find the end of the actual packet's content */
static inline void *packet_end(struct packet *packet)
{
    return packet_start(packet) + packet_size(packet);
}

/** Append new data to the packet */
error_t packet_put(struct packet *packet, const void *data, size_t size);

/** Append new data of literal type to the packet (e.g. integers) */
#define packet_put_literal(packet, data) packet_put(packet, &data, sizeof(data))

/** Set the current end of the packet as the start of the link layer */
static inline void packet_mark_l2_start(struct packet *packet)
{
    packet->l2.raw = packet_end(packet);
}

/** Set the current end of the packet as the start of the network layer */
static inline void packet_mark_l3_start(struct packet *packet)
{
    packet->l3.raw = packet_end(packet);
}

#endif /* KERNEL_NET_PACKET_H */

/** @} */
