/**
 * @brief Berkley Sockets implementation
 *
 * @defgroup networking_sockets BSD Sockets
 * @ingroup networking
 *
 * @{
 */

#ifndef KERNEL_SOCKET_H
#define KERNEL_SOCKET_H

#include <kernel/net.h>
#include <kernel/spinlock.h>
#include <kernel/vfs.h>

#include <libalgo/queue.h>
#include <utils/container_of.h>

struct packet;

/** Socket connection state */
enum socket_state {
    SOCKET_DISCONNECTED, /*!< Connected to a partner */
    SOCKET_CONNECTED,    /*!< Not connected to a remote partner */
};

/** A BSD socket */
struct socket {
    struct file *file;                   /*!< The socket's backing file */
    const struct socket_protocol *proto; /*!< Socket protocol type */
    enum socket_state state;             /*!< Socket connection state*/
    spinlock_t lock;         /*!< Socket wide synchronisation lock */
    void *data;              /*!< Domain-specific socket data */
    struct queue rx_packets; /*!< Packets received */
    spinlock_t rx_lock;      /*!< Synchronisation lock for rx_packets */
};

/** Check whether the socket is connection oriented (TCP, ...) */
static inline bool socket_mode_is_connection(enum socket_type socket_type)
{
    return socket_type == SOCK_STREAM;
}

/** */
static inline void socket_lock(struct socket *socket)
{
    spinlock_acquire(&socket->lock);
}

/** */
static inline void socket_unlock(struct socket *socket)
{
    spinlock_release(&socket->lock);
}

/** Socket node
 *
 *  To be able to associate a socket with a vnode we allocate a larger
 *  structure that contains both the socket and its vnode. We can then
 *  easily jump between the two using @ref container_of
 */
struct socket_node {
    struct socket socket; /*!< The socket */
    struct vnode vnode;   /*!< The vnode */
};

/** @return The socket corresponding to a vnode */
static inline struct socket *socket_from_vnode(struct vnode *vnode)
{
    struct socket_node *socket_node = container_of(vnode, struct socket_node,
                                                   vnode);
    return &socket_node->socket;
}

/** @return The socket's vnode */
static inline struct vnode *socket_node(struct socket *socket)
{
    struct socket_node *socket_node = container_of(socket, struct socket_node,
                                                   socket);
    return &socket_node->vnode;
}

/** Allocate and initialize a new socket */
struct socket *socket_alloc(void);

/** Initialize a socket's underlying protocol.
 *
 * @param domain Communication domain (AF_*).
 * @param type Connection type (SOCK_STREAM, ...).
 * @param proto Packet protocol type
 */
error_t socket_init(struct socket *socket, int domain, int type, int proto);

/** Place a packet inside the socket's receive queue.
 *  Packets received can be read by the user using the recv syscall.
 */
error_t socket_enqueue_packet(struct socket *socket, struct packet *packet);

/** Retreive one packet from the socket's receive queue.
 *  If the queue is empty, return NULL.
 */
struct packet *socket_dequeue_packet(struct socket *socket);

/** Socket communication domain.
 *
 * A socket communication domain can be seen as a kind of 'family'
 * of protocols. The most common example for this would be the
 * IP (AF_INET) family, which supports the UDP/TCP protocols.
 *
 * @ref communication domain
 */
struct socket_domain {
    enum communication_domain domain; /*!< Domain identifier */
    node_t this;                      /*!< Used to list all domains */
    /** Match socket with its protocol, and initialize necessary
     *  per-domain data.
     */
    error_t (*socket_init)(struct socket *, int type, int proto);
};

/** Register a new domain of sockets */
error_t socket_domain_register(struct socket_domain *);

/** */
struct socket_protocol_ops {
    /** Associate socket with a local address */
    error_t (*bind)(struct socket *, struct sockaddr *addr, socklen_t addrlen);
    /** Connect socket to a partner */
    error_t (*connect)(struct socket *, struct sockaddr *addr,
                       socklen_t addrlen);
    /** Send a message through the socket */
    error_t (*sendmsg)(struct socket *, const struct msghdr *, int flags);
    /** Read a message received by the socket */
    error_t (*recvmsg)(struct socket *, struct msghdr *, int flags);
};

/** */
struct socket_protocol {
    int proto;                             /*!< Protocol number */
    enum socket_type type;                 /*!< Protocol type */
    const struct socket_protocol_ops *ops; /*!< Protocol operations **/
};

#endif /* KERNEL_SOCKET_H */

/** @} */
