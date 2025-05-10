#define LOG_DOMAIN "icmp"

#include <kernel/logger.h>
#include <kernel/net/ethernet.h>
#include <kernel/net/icmp.h>
#include <kernel/net/interface.h>
#include <kernel/net/ipv4.h>
#include <kernel/net/packet.h>
#include <kernel/net/route.h>

#include <string.h>

static error_t icmp_handle_echo_request(struct packet *packet)
{
    struct icmp_header *icmphdr = packet_payload(packet);
    struct packet *out_packet;
    struct net_route route;
    error_t ret = E_SUCCESS;

    /* Manually create out packet's routing table entry */
    route.netdev = packet->netdev;
    route.src.ip.sin_addr = packet->l3.ipv4->daddr;
    memcpy(route.src.mac.mac_addr, packet->l2.ethernet->dst,
           sizeof(mac_address_t));
    memcpy(route.dst.mac.mac_addr, packet->l2.ethernet->src,
           sizeof(mac_address_t));
    route.dst.ip.sin_addr = packet->l3.ipv4->saddr;

    /* Copy the request's content with a 'reply' type */
    icmphdr->type = ICMP_ECHO_REPLY;
    icmphdr->checksum = 0;
    icmphdr->checksum = net_internet_checksum(packet_payload(packet),
                                              packet_payload_size(packet));

    out_packet = ipv4_build_packet(&route, IPPROTO_ICMP, packet_payload(packet),
                                   packet_payload_size(packet));
    packet_free(packet);

    if (IS_ERR(out_packet))
        return ERR_FROM_PTR(out_packet);

    ret = packet_send(out_packet);

    packet_free(out_packet);
    return ret;
}

error_t icmp_receive_packet(struct packet *packet)
{
    struct icmp_header *icmphdr = packet_payload(packet);
    error_t ret;

    if (net_internet_checksum(packet_payload(packet),
                              packet_payload_size(packet))) {
        log_warn("invalid checksum");
        ret = E_INVAL;
        goto invalid_packet;
    }

    switch (icmphdr->type) {
    case ICMP_ECHO_REQUEST:
        return icmp_handle_echo_request(packet);
    default:
        log_warn("unsupported packet type: %d", icmphdr->type);
        ret = E_NOT_SUPPORTED;
    }

invalid_packet:
    packet_free(packet);
    return ret;
}
