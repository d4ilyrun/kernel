#include <kernel/devices/ethernet.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/net/packet.h>

#include <string.h>

struct packet *packet_new(size_t packet_size)
{
    struct packet *packet;
    size_t total_size = align_up(sizeof(*packet), PACKET_ALIGN);

    total_size += packet_size;

    packet = kcalloc(1, total_size, KMALLOC_KERNEL);
    if (packet == NULL)
        return PTR_ERR(E_NOMEM);

    packet->allocated_size = packet_size;
    packet->packet_size = 0;
    packet->popped = 0;

    /* Layer 2 header is at the start of the packet */
    packet_mark_l2_start(packet);

    return packet;
}

void packet_free(struct packet *packet)
{
    kfree(packet);
}

error_t packet_send(struct packet *packet)
{
    struct ethernet_device *ethernet = packet->netdev;
    return ethernet->ops->send_packet(ethernet, packet);
}

error_t packet_put(struct packet *skb, const void *data, size_t size)
{
    if (skb->allocated_size - skb->packet_size < size)
        return E_NO_BUFFER_SPACE;

    memcpy(packet_end(skb), data, size);
    skb->packet_size += size;

    return E_SUCCESS;
}

size_t packet_peek(struct packet *packet, void *data, size_t size)
{
    size = MIN(size, packet_read_size(packet));

    if (data)
        memcpy(data, packet_start(packet) + packet->popped, size);

    return size;
}

size_t packet_pop(struct packet *packet, void *data, size_t size)
{
    size = packet_peek(packet, data, size);

    packet->popped += size;

    return size;
}
