#define LOG_DOMAIN "icmp"

#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/net/ethernet.h>
#include <kernel/net/icmp.h>
#include <kernel/net/interface.h>
#include <kernel/net/ipv4.h>
#include <kernel/net/packet.h>
#include <kernel/net/route.h>

#include <string.h>

static u16 icmp_last_identifier = 0;

DECLARE_LLIST(af_inet_icmp_sockets);
DECLARE_SPINLOCK(af_inet_icmp_sockets_lock);

struct icmp_sock {
    u16 identifier; /** ICMP echo identifier */
    struct net_route route;
    node_t this;
    struct socket *socket;
};

/***/
struct icmp_echo_header {
    struct icmp_header icmp;
    __be u16 identifier;
    __be u16 sequence;
};

#define to_isock(_isock) container_of(_isock, struct icmp_sock, this)

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

/** Associate reply with the socket that sent the request */
static error_t icmp_handle_echo_reply(struct packet *packet)
{
    struct icmp_echo_header *icmphdr = packet_payload(packet);
    const struct icmp_sock *isock;

    if (packet_payload_size(packet) < sizeof(*icmphdr))
        return E_INVAL;

    locked_scope (&af_inet_icmp_sockets_lock) {
        FOREACH_LLIST (node, &af_inet_icmp_sockets) {
            isock = to_isock(node);
            if (isock->identifier > icmphdr->identifier) {
                packet_free(packet);
                return E_SUCCESS;
            }
            if (isock->identifier == icmphdr->identifier)
                break;
        }
    }

    return socket_enqueue_packet(isock->socket, packet);
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
    case ICMP_ECHO_REPLY:
        return icmp_handle_echo_reply(packet);
    default:
        log_warn("unsupported packet type: %d", icmphdr->type);
        ret = E_NOT_SUPPORTED;
    }

invalid_packet:
    packet_free(packet);
    return ret;
}

static error_t af_inet_ping_bind(struct socket *socket,
                                 struct sockaddr *sockaddr, socklen_t len)
{
    struct icmp_sock *isock = socket->data;
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

static error_t af_inet_ping_connect(struct socket *socket,
                                    struct sockaddr *sockaddr, socklen_t len)
{
    struct icmp_sock *isock = socket->data;
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
af_inet_ping_send_one(struct socket *socket, const struct iovec *iov, int flags)
{
    struct icmp_sock *isock = socket->data;
    struct icmp_echo_header *icmphdr = iov->iov_base;
    struct packet *packet;

    UNUSED(flags);

    if (iov->iov_len < sizeof(struct icmp_echo_header))
        return E_INVAL;

    if (icmphdr->icmp.type != ICMP_ECHO_REQUEST)
        return E_NOT_SUPPORTED;

    icmphdr->identifier = isock->identifier;
    icmphdr->icmp.checksum = 0;
    icmphdr->icmp.checksum = net_internet_checksum(iov->iov_base, iov->iov_len);

    packet = ipv4_build_packet(&isock->route, socket->proto->proto,
                               iov->iov_base, iov->iov_len);
    if (IS_ERR(packet))
        return ERR_FROM_PTR(packet);

    return packet_send(packet);
}

static error_t
af_inet_ping_sendmsg(struct socket *socket, const struct msghdr *msg, int flags)
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
        ret = af_inet_ping_send_one(socket, &msg->msg_iov[i], flags);
        if (ret)
            break;
    }

    socket_unlock(socket);

    return ret;
}

static error_t
af_inet_ping_recvmsg(struct socket *socket, struct msghdr *msg, int flags)
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

static error_t af_inet_ping_init(struct socket *socket)
{
    struct icmp_sock *isock;

    isock = kcalloc(1, sizeof(*isock), KMALLOC_KERNEL);
    if (isock == NULL)
        return E_NOMEM;

    isock->identifier = icmp_last_identifier++;
    isock->socket = socket;
    socket->data = isock;

    spinlock_acquire(&af_inet_icmp_sockets_lock);
    llist_add_tail(&af_inet_icmp_sockets, &isock->this);
    spinlock_release(&af_inet_icmp_sockets_lock);

    return E_SUCCESS;
}

struct socket_protocol_ops af_inet_icmp_ops = {
    .init = af_inet_ping_init,
    .bind = af_inet_ping_bind,
    .connect = af_inet_ping_connect,
    .sendmsg = af_inet_ping_sendmsg,
    .recvmsg = af_inet_ping_recvmsg,
};
