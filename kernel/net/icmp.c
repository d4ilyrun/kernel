#define LOG_DOMAIN "icmp"

#include <kernel/net/icmp.h>
#include <kernel/net/packet.h>
#include <kernel/logger.h>

error_t icmp_handle_echo_request(struct packet *packet)
{
    struct icmp_header *icmphdr = packet_payload(packet);
}

error_t icmp_receive_packet(struct packet *packet)
{
    struct icmp_header *icmphdr = packet_payload(packet);

    switch (icmphdr->type) {
    case ICMP_ECHO_REQUEST:
        return icmp_handle_echo_request(packet);
    default:
        log_warn("unsupported packet type: %d", icmphdr->type);
        packet_free(packet);
        return E_NOT_SUPPORTED;
    }
}

struct socket_protocol_ops af_inet_icmp_ops = {};

error_t icmp_init(void)
{
    return E_SUCCESS;
}
