#define LOG_DOMAIN "ethernet"

#include <kernel/devices/ethernet.h>
#include <kernel/logger.h>
#include <kernel/net.h>
#include <kernel/net/arp.h>
#include <kernel/net/ethernet.h>
#include <kernel/net/ipv4.h>
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

error_t ethernet_receive_packet(struct packet *packet)
{
    struct ethernet_header *hdr = packet->l2.ethernet;

    packet_set_l2_size(packet, sizeof(struct ethernet_header));

    switch (ntoh(hdr->protocol)) {
    case ETH_PROTO_ARP:
        return arp_receive_packet(packet);
    case ETH_PROTO_IP:
        return ipv4_receive_packet(packet);
    default:
        log_warn("unsupported protocol type: " FMT16, ntoh(hdr->protocol));
        return E_NOT_SUPPORTED;
    }
}
