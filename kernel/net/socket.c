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
    INIT_WAITQUEUE(socket->rx_blocked);

    return socket->domain->socket_init(socket, type, proto);
}

/*
 * FIXME: We MUST limit the number of packets/bytes inside the RX queue.
 *        We should use a pre-allocated per-socket ringbuffer and make
 *        struct packet point to an external buffer (socket_buffer API).
 */
error_t socket_enqueue_packet(struct socket *socket, struct packet *packet)
{
    spinlock_acquire(&socket->rx_lock);
    queue_enqueue(&socket->rx_packets, &packet->rx_this);
    spinlock_release(&socket->rx_lock);
    waitqueue_dequeue(&socket->rx_blocked);

    return E_SUCCESS;
}

/*
 *
 */
struct packet *socket_dequeue_packet(struct socket *socket, bool nonblock)
{
    struct packet *packet = NULL;
    node_t *node;

retry:
    spinlock_acquire(&socket->rx_lock);
    if (!queue_is_empty(&socket->rx_packets)) {
        node = queue_dequeue(&socket->rx_packets);
        packet = container_of(node, struct packet, rx_this);
    } else if (!nonblock) {
        spinlock_acquire(&socket->rx_blocked.lock);
        spinlock_release(&socket->rx_lock);
        waitqueue_enqueue_locked(&socket->rx_blocked, current);
        goto retry;
    } else
        packet = PTR_ERR(E_WOULD_BLOCK);

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
    ssize_t received = 0;

    msg->msg_flags = 0;

    if (flags & ~O_NONBLOCK) {
        not_implemented("recvmsg: flags (%x)", flags & ~O_NONBLOCK);
        return -E_NOT_SUPPORTED;
    }

    if (msg->msg_name) {
        not_implemented("overriding source address in recvmsg");
        return -E_NOT_IMPLEMENTED;
    }

    packet = socket_dequeue_packet(socket, flags & O_NONBLOCK);
    if (IS_ERR(packet))
        return -ERR_FROM_PTR(packet);

    packet_pop(packet, NULL, packet_header_size(packet));
    for (size_t i = 0; i < msg->msg_iovlen; ++i) {
        struct iovec *iov = &msg->msg_iov[i];
        size_t popped;

        popped = packet_pop(packet, iov->iov_base, iov->iov_len);
        received += popped;
        if (popped < iov->iov_len)
            break;
    }

    if (packet_read_size(packet))
        msg->msg_flags |= MSG_TRUNC;

    packet_free(packet);

    return received;
}
