#include <kernel/net/packet.h>
#include <kernel/socket.h>

#include <libalgo/linked_list.h>
#include <utils/macro.h>

DECLARE_LLIST(socket_domains);

static int socket_domain_compare(const void *left, const void *right)
{
    struct socket_domain *a = container_of(left, struct socket_domain, this);
    struct socket_domain *b = container_of(right, struct socket_domain, this);

    RETURN_CMP(a->domain, b->domain);
}

error_t socket_domain_register(struct socket_domain *domain)
{
    if (!llist_insert_sorted_unique(&socket_domains, &domain->this,
                                    socket_domain_compare))
        return E_EXIST;

    return E_SUCCESS;
}

error_t socket_init(struct socket *socket, int domain, int type, int proto)
{
    struct socket_domain to_find = {.domain = domain};
    node_t *node;

    node = llist_find_first(&socket_domains, &to_find.this,
                            socket_domain_compare);
    if (!node)
        return E_AF_NOT_SUPPORTED;

    socket->domain = container_of(node, struct socket_domain, this);
    socket->state = SOCKET_DISCONNECTED;

    INIT_QUEUE(socket->rx_packets);
    INIT_SPINLOCK(socket->rx_lock);
    INIT_SPINLOCK(socket->lock);

    return socket->domain->socket_init(socket, type, proto);
}

error_t socket_enqueue_packet(struct socket *socket, struct packet *packet)
{
    spinlock_acquire(&socket->rx_lock);
    queue_enqueue(&socket->rx_packets, &packet->rx_this);
    spinlock_release(&socket->rx_lock);

    return E_SUCCESS;
}

struct packet *socket_dequeue_packet(struct socket *socket)
{
    struct packet *packet = NULL;
    node_t *node;

    spinlock_acquire(&socket->rx_lock);
    if (!queue_is_empty(&socket->rx_packets)) {
        node = queue_dequeue(&socket->rx_packets);
        packet = container_of(node, struct packet, rx_this);
    }
    spinlock_release(&socket->rx_lock);

    return packet;
}

/*
 * Common sendmsg() implementation for datagram sockets.
 */
ssize_t
socket_dgram_sendmsg(struct socket *socket, const struct msghdr *msg, int flags,
                     ssize_t (*send_one)(struct socket *socket,
                                         const struct iovec *iov, int flags))
{
    ssize_t sent = 0;
    ssize_t ret;

    if (flags) {
        not_implemented("recvmsg: flags");
        return -E_NOT_SUPPORTED;
    }

    socket_lock(socket);
    for (size_t i = 0; i < msg->msg_iovlen; ++i) {
        ret = send_one(socket, &msg->msg_iov[i], flags);
        if (ret < 0) {
            sent = ret;
            break;
        }
        sent += ret;
    }
    socket_unlock(socket);

    return sent;
}

/*
 * Common recvmsg() implementation for datagram sockets.
 */
ssize_t
socket_dgram_recvmsg(struct socket *socket, struct msghdr *msg, int flags)
{
    struct packet *packet;
    struct iovec *iov;
    ssize_t received = 0;

    if (flags) {
        not_implemented("recvmsg: flags");
        return -E_NOT_SUPPORTED;
    }

    if (msg->msg_name) {
        not_implemented("overriding source address in recvmsg");
        return -E_NOT_IMPLEMENTED;
    }

    // TODO: Block until packets are received (check flags/options for NOBLOCK)
    packet = socket_dequeue_packet(socket);
    if (!packet)
        return -E_WOULD_BLOCK;

    packet_pop(packet, NULL, packet_header_size(packet));

    for (size_t i = 0; i < msg->msg_iovlen; ++i) {
        size_t popped;

        iov = &msg->msg_iov[i];
        if (iov->iov_len > packet_read_size(packet)) {
            // TODO: Block until enough data
        }

        popped = packet_pop(packet, iov->iov_base, iov->iov_len);
        received += popped;
        if (popped < iov->iov_len)
            break;
    }

    packet_free(packet);

    return received;
}
