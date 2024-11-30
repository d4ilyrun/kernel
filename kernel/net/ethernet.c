#include <kernel/devices/ethernet.h>
#include <kernel/logger.h>
#include <kernel/net.h>
#include <kernel/net/arp.h>
#include <kernel/net/ethernet.h>
#include <kernel/net/packet.h>

#include <string.h>

void ethernet_fill_packet(struct packet *packet, enum ethernet_type proto,
                          const mac_address_t dst)
{
    struct ethernet_device *dev = packet->netdev;
    struct ethernet_header hdr;

    memcpy(hdr.dst, dst, sizeof(mac_address_t));
    memcpy(hdr.src, dev->mac, sizeof(mac_address_t));
    hdr.protocol = htons(proto);

    packet_mark_l2_start(packet);
    packet_put(packet, &hdr, sizeof(struct ethernet_header));
}

/* FIXME: This should ideally be done in another dedicated thread.
 *        Since this is meant to be called from an interrupt handler,
 *        we are currently just essentially blocking the whole kernel ...
 */
error_t ethernet_receive_packet(struct packet *packet)
{
    struct ethernet_header *hdr = packet->l2.ethernet;

    packet->l3.raw = packet->l2.raw + sizeof(struct ethernet_header);

    switch (ntoh(hdr->protocol)) {
    case ETH_PROTO_ARP:
        return arp_receive_packet(packet);
    case ETH_PROTO_IP:
        log_warn("eth", "Not implemented: IPv4");
        return E_NOT_IMPLEMENTED;
    }

    return E_NOT_SUPPORTED;
}
