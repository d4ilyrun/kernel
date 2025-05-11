#define LOG_DOMAIN "ipv4"

#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/net.h>
#include <kernel/net/arp.h>
#include <kernel/net/ethernet.h>
#include <kernel/net/icmp.h>
#include <kernel/net/interface.h>
#include <kernel/net/ipv4.h>
#include <kernel/net/packet.h>
#include <kernel/net/route.h>
#include <kernel/socket.h>

#include <utils/macro.h>

#include <string.h>

/** Domain specific data for AF_INET sockets */
struct af_inet_sock {
    struct socket *socket;
    struct net_route route; /** Routing configuration */
    enum ip_protocol proto; /** Protocol number */
    node_t this;            /** Used to list currently active sockets */
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
    /* checksum field needs to be replaced with 0 */
    iphdr->check = 0;
    iphdr->check = net_internet_checksum((void *)iphdr,
                                         ipv4_header_size(iphdr));

    return iphdr->check;
}

error_t ipv4_receive_packet(struct packet *packet)
{
    const struct ipv4_header *iphdr = packet->l3.ipv4;
    struct af_inet_sock *isock;
    struct packet *clone;
    size_t total_len;
    size_t hdr_len;
    error_t ret = E_SUCCESS;

    total_len = ntohs(packet->l3.ipv4->tot_len);
    hdr_len = ipv4_header_size(packet->l3.ipv4);
    packet_set_l3_size(packet, hdr_len);

    if (packet_payload_size(packet) < total_len - hdr_len) {
        log_err("ERROR: payload size < actual size: %ld != %ld",
                packet_payload_size(packet), total_len - hdr_len);
        ret = E_INVAL;
        goto invalid_packet;
    }

    if (ipv4_is_fragmented(iphdr)) {
        not_implemented("IP segmentation");
        ret = E_NOT_IMPLEMENTED;
        goto invalid_packet;
    }

    if (ipv4_is_multicast(iphdr->daddr) || ipv4_is_broadcast(iphdr->daddr)) {
        not_implemented("Broadcast/Multicast: " FMT_IP, LOG_IP(iphdr->daddr));
        ret = E_NOT_IMPLEMENTED;
        goto invalid_packet;
    }

    if (net_internet_checksum(packet->l3.raw, ipv4_header_size(iphdr))) {
        log_warn("invalid checksum");
        ret = E_INVAL;
        goto invalid_packet;
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
            (isock->route.src.ip.sin_family != AF_UNSPEC &&
             isock->route.src.ip.sin_addr != iphdr->daddr) ||
            (isock->route.dst.ip.sin_family != AF_UNSPEC &&
             isock->route.dst.ip.sin_addr != iphdr->saddr)) {
            socket_unlock(isock->socket);
            continue;
        }
        socket_unlock(isock->socket);
        /*
         * The original packet should be sent to the 'regular' socket
         * that matches its destination. Since there can only be one anyway,
         * which isn't the case for raw sockets, this makes things way easier.
         */
        clone = packet_clone(packet);
        if (!IS_ERR(clone))
            socket_enqueue_packet(isock->socket, clone);
    }
    spinlock_release(&af_inet_raw_sockets_lock);

    /* Not for us */
    if (!net_interface_find(packet->l3.ipv4->daddr))
        goto invalid_packet;

    switch (iphdr->protocol) {
    case IPPROTO_ICMP:
        return icmp_receive_packet(packet);
    default:
        ret = E_NOT_SUPPORTED;
        break;
    }

invalid_packet:
    packet_free(packet);
    return ret;
}

struct packet *ipv4_build_packet(const struct net_route *route, u8 proto,
                                 const void *payload, size_t size)
{
    struct packet *packet;
    struct ipv4_header iphdr;
    error_t ret;

    packet = packet_new(size + sizeof(struct ipv4_header) +
                        sizeof(struct ethernet_header));
    if (IS_ERR(packet)) {
        log_err("failed to allocate packet: %s",
                err_to_str(ERR_FROM_PTR(packet)));
        return packet;
    }

    packet->netdev = route->netdev;
    ethernet_fill_packet(packet, ETH_PROTO_IP, route->dst.mac.mac_addr);

    iphdr.saddr = route->src.ip.sin_addr;
    iphdr.daddr = route->dst.ip.sin_addr;
    iphdr.protocol = proto;
    iphdr.tot_len = ntohs(size + sizeof(struct ipv4_header));
    iphdr.version = IPV4_VERSION;
    iphdr.ihl = IPV4_MIN_LENGTH / sizeof(uint32_t);
    iphdr.ttl = IPV4_DEFAULT_TTL;
    iphdr.frag_off = 0;
    ipv4_compute_checksum(&iphdr);

    if (!ipv4_validate_header(&iphdr)) {
        log_err("invalid ipv4 header");
        log_array_8((u8 *)&iphdr, ipv4_header_size(&iphdr));
        ret = E_INVAL;
        goto release_packet;
    }

    packet_put(packet, &iphdr, sizeof(iphdr));
    packet_set_l3_size(packet, ipv4_header_size(&iphdr));

    ret = packet_put(packet, payload, size);
    if (ret)
        goto release_packet;

    return packet;

release_packet:
    packet_free(packet);
    return PTR_ERR(ret);
}

static error_t af_inet_raw_bind(struct socket *socket,
                                struct sockaddr *sockaddr, socklen_t len)
{
    struct af_inet_sock *isock = socket->data;
    struct sockaddr_in *src = (struct sockaddr_in *)sockaddr;
    struct net_interface *iface;
    error_t ret = E_SUCCESS;

    if (sockaddr->sa_family != AF_INET)
        return E_AF_NOT_SUPPORTED;

    if (len != sizeof(struct sockaddr_in))
        return E_INVAL;

    iface = net_interface_find(src->sin_addr);
    if (iface == NULL)
        return E_ADDR_NOT_AVAILABLE;

    isock->route.src.ip = *src;
    isock->route.netdev = iface->netdev;

    return ret;
}

static error_t af_inet_raw_connect(struct socket *socket,
                                   struct sockaddr *sockaddr, socklen_t len)
{
    struct af_inet_sock *isock = socket->data;
    struct sockaddr_in *dst = (struct sockaddr_in *)sockaddr;
    struct net_route route;
    error_t ret = E_SUCCESS;

    if (sockaddr->sa_family != AF_INET)
        return E_AF_NOT_SUPPORTED;

    if (len != sizeof(struct sockaddr_in))
        return E_INVAL;

    socket_lock(socket);

    ret = net_route_compute(&route, dst);
    if (ret)
        goto exit_connect;

    /* The source address may already have been chosen by bind() */
    if (isock->route.src.ip.sin_family != AF_UNSPEC) {
        route.src = isock->route.src;
        if (route.netdev != isock->route.netdev)
            return E_NET_UNREACHABLE;
    }

    isock->route = route;
    socket->state = SOCKET_CONNECTED;

exit_connect:
    socket_unlock(socket);
    return ret;
}

static error_t
af_inet_raw_send_one(struct socket *socket, const struct iovec *iov, int flags)
{
    struct af_inet_sock *isock = socket->data;
    struct packet *packet;

    UNUSED(flags);

    packet = ipv4_build_packet(&isock->route, isock->proto, iov->iov_base,
                               iov->iov_len);
    if (IS_ERR(packet))
        return ERR_FROM_PTR(packet);

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

    packet_free(packet);

    return ret;
}

static error_t af_inet_raw_init(struct socket *socket)
{
    struct af_inet_sock *isock = NULL;

    isock = kcalloc(1, sizeof(*isock), KMALLOC_KERNEL);
    if (isock == NULL)
        return E_NOMEM;

    isock->proto = socket->proto->proto;
    isock->socket = socket;
    socket->data = isock;

    spinlock_acquire(&af_inet_raw_sockets_lock);
    llist_add(&af_inet_raw_sockets, &isock->this);
    spinlock_release(&af_inet_raw_sockets_lock);

    return E_SUCCESS;
}

static const struct socket_protocol_ops af_inet_raw_ops = {
    .init = af_inet_raw_init,
    .bind = af_inet_raw_bind,
    .connect = af_inet_raw_connect,
    .sendmsg = af_inet_raw_sendmsg,
    .recvmsg = af_inet_raw_recvmsg,
};

static const struct socket_protocol af_inet_protocols[] = {
    {
        .type = SOCK_DGRAM,
        .proto = IPPROTO_ICMP,
        .ops = &af_inet_icmp_ops,
    },
    {
        .type = SOCK_RAW,
        .ops = &af_inet_raw_ops,
    },
};

static error_t af_inet_socket_init(struct socket *socket, int type, int proto)
{
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

    socket->proto = ip_proto;

    return ip_proto->ops->init(socket);
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
