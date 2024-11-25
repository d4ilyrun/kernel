#include <kernel/net.h>
#include <kernel/net/arp.h>
#include <kernel/net/ethernet.h>
#include <kernel/net/ipv4.h>
#include <kernel/net/packet.h>

#include <utils/macro.h>

static size_t ipv4_header_size(struct ipv4_header *iphdr)
{
    return iphdr->ihl * sizeof(uint32_t);
}

bool ipv4_validate_packet(struct ipv4_header *iphdr)
{
    if (iphdr->version != IPV4_VERSION)
        return false;

    if (iphdr->tot_len < IPV4_MIN_LENGTH)
        return false;

    if (iphdr->ihl * sizeof(uint32_t) != sizeof(struct ipv4_header))
        return false;

    return true;
}

/** Reference implementation of the IPv4 checksum algorithm.
 *  @see RFC1071 - 4.1
 */
static uint16_t ipv4_compute_checksum(struct ipv4_header *iphdr)
{
    size_t count = ipv4_header_size(iphdr);
    uint16_t *word = (uint16_t *)iphdr;
    uint32_t checksum;
    uint32_t sum = 0;

    /* checksum field needs to be replaced with 0 */
    iphdr->check = 0;

    while (count > 0) {
        sum += *word++;
        count -= sizeof(uint16_t);
    }

    if (count > 0)
        sum += *word;

    while (sum > 0xFFFF)
        sum = (sum & 0xFFFF) + (sum >> 16);

    checksum = ~sum;
    iphdr->check = checksum;

    return checksum;
}

void ipv4_fill_packet(struct packet *packet, struct ipv4_header *iphdr)
{
    const mac_address_t *dst_mac = arp_get(iphdr->saddr);

    if (dst_mac == NULL) {
        /* TODO: ARP request */
        return;
    }

    ethernet_fill_packet(packet, ETH_PROTO_IP, *dst_mac);

    iphdr->version = IPV4_VERSION;
    iphdr->ihl = IPV4_MIN_LENGTH / sizeof(uint32_t);
    ipv4_compute_checksum(iphdr);

    packet_mark_l3_start(packet);
    packet_put(packet, &iphdr, sizeof(iphdr));
}
