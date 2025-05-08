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
    struct socket_domain *socket_domain;
    node_t *node;

    node = llist_find_first(socket_domains, &to_find.this,
                            socket_domain_compare);
    if (!node)
        return E_AF_NOT_SUPPORTED;

    socket->state = SOCKET_DISCONNECTED;

    INIT_QUEUE(socket->rx_packets);
    INIT_SPINLOCK(socket->rx_lock);

    socket_domain = container_of(node, struct socket_domain, this);
    return socket_domain->socket_init(socket, type, proto);
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
