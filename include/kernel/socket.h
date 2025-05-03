/**
 * @{
 */

#ifndef KERNEL_SOCKET_H
#define KERNEL_SOCKET_H

#include <kernel/vfs.h>

#include <utils/container_of.h>

/***/
struct socket {
    struct file *file; /*!< The socket's backing file */
};

/***/
struct socket_node {
    struct socket socket;
    struct vnode vnode;
};

static inline struct socket *socket_from_vnode(struct vnode *vnode)
{
    struct socket_node *socket_node = container_of(vnode, struct socket_node,
                                                   vnode);
    return &socket_node->socket;
}

static inline struct vnode *socket_node(struct socket *socket)
{
    struct socket_node *socket_node = container_of(socket, struct socket_node,
                                                   socket);
    return &socket_node->vnode;
}

/** Allocate and initialize a new socket */
struct socket *socket_alloc(void);

#endif /* KERNEL_SOCKET_H */

/** @{ */
