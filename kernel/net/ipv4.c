#define LOG_DOMAIN "ipv4"

#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/net.h>
#include <kernel/net/arp.h>
#include <kernel/net/ethernet.h>
#include <kernel/net/interface.h>
#include <kernel/net/ipv4.h>
#include <kernel/net/packet.h>
#include <kernel/socket.h>

#include <utils/macro.h>

#include <string.h>

/** Domain specific data for AF_INET sockets */
struct af_inet_sock {
    struct ethernet_device *netdev;
    __be struct sockaddr_in saddr_in; /** Source IP address */
    __be struct sockaddr_in daddr_in; /** Destination IP address */
    struct sockaddr_mac daddr_mac;    /** Destination MAC address*/
    enum ip_protocol proto;           /** Protocol number */
};

static size_t ipv4_header_size(const struct ipv4_header *iphdr)
{
    return iphdr->ihl * sizeof(uint32_t);
}

bool ipv4_validate_header(const struct ipv4_header *iphdr)
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

static error_t af_inet_raw_connect(struct socket *socket,
                                   struct sockaddr *sockaddr, socklen_t len)
{
    struct af_inet_sock *isock = socket->data;
    struct sockaddr_in *sin = (struct sockaddr_in *)sockaddr;
    const struct subnet *subnet;
    const mac_address_t *daddr_mac;
    error_t ret = E_SUCCESS;

    if (sockaddr->sa_family != AF_INET)
        return E_AF_NOT_SUPPORTED;

    if (len != sizeof(struct sockaddr_in))
        return E_INVAL;

    socket_lock(socket);

    /* routing: find the destination address's subnet
     * its source ip and network device shall be used for the packet */
    subnet = net_interface_find_subnet(ntoh(sin->sin_addr));
    if (!subnet) {
        ret = E_NET_UNREACHABLE;
        goto exit_connect;
    }

    isock->netdev = subnet->interface->netdev;
    isock->daddr_in = *sin;
    isock->saddr_in = (struct sockaddr_in){
        .sin_addr = hton(subnet->ip),
        .sin_family = AF_INET,
        .sin_port = 0, // TODO
    };

    daddr_mac = arp_get(isock->daddr_in.sin_addr);
    if (daddr_mac == NULL) {
        /* TODO: ARP request */
        ret = E_NET_UNREACHABLE;
        goto exit_connect;
    }

    memcpy(isock->daddr_mac.mac_addr, daddr_mac, sizeof(mac_address_t));

    socket->state = SOCKET_CONNECTED;

exit_connect:
    socket_unlock(socket);
    return ret;
}

static error_t
af_inet_raw_send_one(struct socket *socket, const struct iovec *iov, int flags)
{
    struct af_inet_sock *isock = socket->data;
    struct ipv4_header iphdr = {0};
    struct packet *packet;
    error_t ret;

    UNUSED(flags);

    packet = packet_new(iov->iov_len + sizeof(struct ipv4_header) +
                        sizeof(struct ethernet_header));
    if (IS_ERR(packet)) {
        ret = ERR_FROM_PTR(packet);
        log_err("failed to allocate packet: %s", err_to_str(ret));
        return ret;
    }

    packet->netdev = isock->netdev;

    ethernet_fill_packet(packet, ETH_PROTO_IP, isock->daddr_mac.mac_addr);

    iphdr.saddr = isock->saddr_in.sin_addr;
    iphdr.daddr = isock->daddr_in.sin_addr;
    iphdr.protocol = isock->proto;
    iphdr.tot_len = ntohs(iov->iov_len + sizeof(struct ipv4_header));
    iphdr.version = IPV4_VERSION;
    iphdr.ihl = IPV4_MIN_LENGTH / sizeof(uint32_t);
    iphdr.ttl = IPV4_DEFAULT_TTL;
    ipv4_compute_checksum(&iphdr);

    if (!ipv4_validate_header(&iphdr)) {
        log_err("invalid ipv4 header");
        log_array_8((u8 *)&iphdr, ipv4_header_size(&iphdr));
        return E_INVAL;
    }

    packet_mark_l3_start(packet);
    packet_put(packet, &iphdr, sizeof(iphdr));

    ret = packet_put(packet, iov->iov_base, iov->iov_len);
    if (ret)
        return ret;

    return packet_send(packet);
}

static error_t
af_inet_raw_sendmsg(struct socket *socket, const struct msghdr *msg, int flags)
{
    error_t ret = E_SUCCESS;

    if (flags) {
        not_implemented("recvmsg: flags");
        return E_NOT_SUPPORTED;
    }

    if (msg->msg_name) {
        not_implemented("overriding destination address in sendmsg");
        if (msg->msg_namelen != sizeof(struct sockaddr_in))
            return E_INVAL;
        return E_NOT_IMPLEMENTED;
    }

    socket_lock(socket);

    for (size_t i = 0; i < msg->msg_iovlen; ++i) {
        ret = af_inet_raw_send_one(socket, &msg->msg_iov[i], flags);
        if (ret)
            break;
    }

    socket_unlock(socket);

    return ret;
}

static const struct socket_protocol af_inet_protocols[] = {
    {
        .type = SOCK_RAW,
        .connect = af_inet_raw_connect,
        .sendmsg = af_inet_raw_sendmsg,
    },
};

static error_t af_inet_socket_init(struct socket *socket, int type, int proto)
{
    struct af_inet_sock *isock = NULL;
    const struct socket_protocol *ip_proto = NULL;
    error_t ret;

    ret = E_PROTO_NOT_SUPPORTED;
    for (size_t i = 0; i < ARRAY_SIZE(af_inet_protocols); ++i) {
        /* Protocol type not matched for raw sockets */
        if (type == SOCK_RAW && af_inet_protocols[i].type == SOCK_RAW) {
            ip_proto = &af_inet_protocols[i];
            break;
        }
        if (af_inet_protocols[i].proto == proto) {
            if ((int)af_inet_protocols[i].type == type) {
                ip_proto = &af_inet_protocols[i];
                break;
            }
            ret = E_SOCK_T_NOT_SUPPORTED;
        }
    }

    if (!ip_proto)
        return ret;

    isock = kcalloc(1, sizeof(*isock), KMALLOC_KERNEL);
    if (isock == NULL)
        return E_NOMEM;

    socket->proto = ip_proto;
    socket->data = isock;
    isock->proto = proto;

    return E_SUCCESS;
}

struct socket_domain af_inet = {
    .domain = AF_INET,
    .socket_init = af_inet_socket_init,
};

void ipv4_init(void)
{
    error_t err = socket_domain_register(&af_inet);
    log_info("Registering AF_INET domain: %s", err_to_str(err));
}
