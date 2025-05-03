/**
 * Socket pseudo filesystem
 *
 * By design, sockets need to be interacted with just like
 * any other regular file. For this, we need to create a pseudo
 * filesystem dedicated to allocating them.
 *
 * A pseudo filesystem isn't a real filesystem. We just need to
 * allocate vnodes with the required operations to create and
 * interact with the socket.
 */

#define LOG_DOMAIN "socket"

#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/socket.h>
#include <kernel/vfs.h>

static struct file_operations socket_fops = {};

static void socket_vnode_release(struct vnode *vnode)
{
    kfree(container_of(vnode, struct socket_node, vnode));
}

static struct vnode_operations socket_vnode_ops = {
    .release = socket_vnode_release,
};

struct socket *socket_alloc(void)
{
    struct socket_node *node;
    struct socket *socket;
    struct vnode *vnode;
    struct file *file;

    node = kcalloc(1, sizeof(*node), KMALLOC_DEFAULT);
    if (node == NULL)
        return PTR_ERR(E_NOMEM);

    /* Increase refcount */
    vnode = vfs_vnode_acquire(&node->vnode, NULL);
    socket = &node->socket;

    /* No real filesystem, just standalone vnodes */
    vnode->fs = NULL;
    vnode->operations = &socket_vnode_ops;
    vnode->type = VNODE_SOCKET;

    file = file_open(vnode, &socket_fops);
    if (IS_ERR(file)) {
        log_err("Failed to open socket file: %s",
                err_to_str(ERR_FROM_PTR(file)));
        vfs_vnode_release(vnode);
        return (void *)file;
    }

    socket->file = file;
    file->priv = socket;

    return socket;
}
