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
    struct socket *socket;
    struct ethernet_device *netdev;
    __be struct sockaddr_in saddr_in; /** Source IP address */
    __be struct sockaddr_in daddr_in; /** Destination IP address */
    struct sockaddr_mac daddr_mac;    /** Destination MAC address*/
    enum ip_protocol proto;           /** Protocol number */
    node_t this; /** Used to list currently active sockets */
};

static DECLARE_LLIST(af_inet_raw_sockets);
static DECLARE_SPINLOCK(af_inet_raw_sockets_lock);

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

error_t ipv4_receive_packet(struct packet *packet)
{
    const struct ipv4_header *iphdr = packet->l3.ipv4;
    struct af_inet_sock *isock;
    size_t total_len;
    size_t hdr_len;

    total_len = htons(packet->l3.ipv4->tot_len);
    hdr_len = ipv4_header_size(packet->l3.ipv4);
    packet_set_l3_size(packet, hdr_len);

    if (packet_payload_size(packet) != (total_len - hdr_len)) {
        log_err("ERROR: payload size != actual size: %ld != %ld",
                packet_payload_size(packet), total_len - hdr_len);
        return E_INVAL;
    }

    if (ipv4_is_fragmented(iphdr)) {
        not_implemented("IP segmentation");
        return E_NOT_IMPLEMENTED;
    }

    /* All incoming traffic is intercepted by RAW sockets:
     * - If protocol is the same
     * - If binded to the destination address (when binded)
     * - If connected to the source address (when connected)
     */
    spinlock_acquire(&af_inet_raw_sockets_lock);
    FOREACH_LLIST (node, af_inet_raw_sockets) {
        isock = container_of(node, struct af_inet_sock, this);
        socket_lock(isock->socket);
        if (isock->proto != iphdr->protocol ||
            (isock->saddr_in.sin_family != AF_UNSPEC &&
             isock->saddr_in.sin_addr != iphdr->daddr) ||
            (isock->daddr_in.sin_family != AF_UNSPEC &&
             isock->daddr_in.sin_addr != iphdr->saddr)) {
            socket_unlock(isock->socket);
            continue;
        }
        socket_unlock(isock->socket);
        socket_enqueue_packet(isock->socket, packet);
    }
    spinlock_release(&af_inet_raw_sockets_lock);

    switch (iphdr->protocol) {
    default:
        break;
    }

    return E_SUCCESS;
}

static error_t af_inet_raw_bind(struct socket *socket,
                                struct sockaddr *sockaddr, socklen_t len)
{
    struct af_inet_sock *isock = socket->data;
    struct sockaddr_in *sin = (struct sockaddr_in *)sockaddr;
    error_t ret = E_SUCCESS;

    if (sockaddr->sa_family != AF_INET)
        return E_AF_NOT_SUPPORTED;

    if (len != sizeof(struct sockaddr_in))
        return E_INVAL;

    isock->saddr_in = *sin;

    spinlock_acquire(&af_inet_raw_sockets_lock);
    llist_add(&af_inet_raw_sockets, &isock->this);
    spinlock_release(&af_inet_raw_sockets_lock);

    return ret;
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

    daddr_mac = arp_get(isock->daddr_in.sin_addr);
    if (daddr_mac == NULL) {
        /* TODO: ARP request */
        ret = E_NET_UNREACHABLE;
        goto exit_connect;
    }

    /* The source address may already have been chosen by bind() */
    if (isock->saddr_in.sin_family == AF_UNSPEC) {
        isock->saddr_in = (struct sockaddr_in){
            .sin_addr = hton(subnet->ip),
            .sin_family = AF_INET,
            .sin_port = -1, // Not used in raw sockets
        };
        llist_add(&af_inet_raw_sockets, &isock->this);
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

static error_t
af_inet_raw_recvmsg(struct socket *socket, struct msghdr *msg, int flags)
{
    error_t ret = E_SUCCESS;
    struct packet *packet;
    struct iovec *iov;

    if (flags) {
        not_implemented("recvmsg: flags");
        return E_NOT_SUPPORTED;
    }

    if (socket->state != SOCKET_CONNECTED) {
        if (msg->msg_namelen != sizeof(struct sockaddr_in))
            return E_INVAL;
        not_implemented("overriding source address in recvmsg");
        return E_NOT_IMPLEMENTED;
    }

    // TODO: Block until packets are received (check flags/options for NOBLOCK)
    packet = socket_dequeue_packet(socket);
    if (!packet)
        return E_WOULD_BLOCK;

    packet_pop(packet, NULL, packet_header_size(packet));

    for (size_t i = 0; i < msg->msg_iovlen; ++i) {
        iov = &msg->msg_iov[i];
        if (iov->iov_len > packet_read_size(packet)) {
            // TODO: Block until enough data
            ret = E_WOULD_BLOCK;
        }
        if (packet_pop(packet, iov->iov_base, iov->iov_len) < iov->iov_len)
            break;
    }

    return ret;
}

static const struct socket_protocol af_inet_protocols[] = {
    {
        .type = SOCK_RAW,
        .bind = af_inet_raw_bind,
        .connect = af_inet_raw_connect,
        .sendmsg = af_inet_raw_sendmsg,
        .recvmsg = af_inet_raw_recvmsg,
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
    isock->socket = socket;

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
