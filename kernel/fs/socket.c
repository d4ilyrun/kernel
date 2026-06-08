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
 *
 * ## Reference count
 *
 * Individual sockets are reference counted via their vnode's reference count.
 * This reference count must be updated via socket_get/put() when accessing
 * a socket (locally, or when storing a reference for later (e.g. connected
 * sockets)).
 */

#define LOG_DOMAIN "socket"

#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/net/packet.h>
#include <kernel/socket.h>
#include <kernel/timer.h>
#include <kernel/vfs.h>

#include <limits.h>

static struct file_operations socket_fops;

static void socket_vnode_release(struct vnode *vnode)
{
    struct socket *socket = socket_from_vnode(vnode);

    if (socket->proto && socket->proto->ops->release)
        socket->proto->ops->release(socket);

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
    struct stat *stat;

    node = kcalloc(1, sizeof(*node), KMALLOC_KERNEL);
    if (node == NULL)
        return PTR_ERR(E_NOMEM);

    /* Increase refcount */
    vnode = vnode_acquire(&node->vnode, NULL);
    socket = &node->socket;

    /* No real filesystem, just standalone vnodes */
    vnode->fs = NULL;
    vnode->operations = &socket_vnode_ops;
    vnode->type = VNODE_SOCKET;

    stat = &vnode->stat;
    clock_get_time(&stat->st_mtim);
    stat->st_ctim = stat->st_mtim;
    stat->st_mode = S_IRWU | S_IRWG | S_IRWO;
    stat->st_nlink = 1;

    file = file_open(vnode, &socket_fops);
    if (IS_ERR(file)) {
        log_err("Failed to open socket file: %pE", file);
        vnode_release(vnode);
        return (void *)file;
    }

    socket->file = file;
    file->priv = socket;

    return socket;
}

/* Close a socket.
 *
 * Once a socket has been closed it must no be reachable by other sockets
 * inside the same domain (i.e. bind(), connect(), ...).
 *
 * For connection-mode sockets closing the file descriptor causes the ongoing
 * connection to be closed. The actual socket is released once its reference
 * count reaches zero. If the protocol's close() operation isn't atomic, it
 * must hold an additional reference to the socket for the duration of the
 * close operation.
 */
static void socket_close(struct file *file)
{
    struct socket *socket = file->priv;

    /*
     * Discard all previously received packets.
     */
    spinlock_acquire(&socket->rx_lock);
    while (!queue_is_empty(&socket->rx_packets)) {
        struct packet *pkt;

        pkt = queue_dequeue_entry(&socket->rx_packets, struct packet, rx_this);
        packet_free(pkt);
    }
    spinlock_release(&socket->rx_lock);

    /*
     * Close connection and make socket unreachable.
     */
    if (socket->proto->ops->close)
        socket->proto->ops->close(socket);
}

/*
 *
 */
static error_t
socket_bind(struct file *file, struct sockaddr *addr, socklen_t addr_len)
{
    struct socket *socket = file->priv;
    error_t err;

    if (socket->file->vnode->type != VNODE_SOCKET)
        return E_NOT_SOCKET;

    err = socket->domain->verify_addr(addr, addr_len);
    if (err)
        return -err;

    return socket->proto->ops->bind(socket, addr, addr_len);
}

static error_t
socket_connect(struct file *file, struct sockaddr *addr, socklen_t addr_len)
{
    struct socket *socket = file->priv;
    error_t err;

    if (socket->file->vnode->type != VNODE_SOCKET)
        return E_NOT_SOCKET;

    err = socket->domain->verify_addr(addr, addr_len);
    if (err)
        return -err;

    return socket->proto->ops->connect(socket, addr, addr_len);
}

/*
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/sendmsg.html
 */
static ssize_t
socket_sendmsg(struct file *file, const struct msghdr *msg, int flags)
{
    struct socket *socket = file->priv;
    error_t err;

    if (file->vnode->type != VNODE_SOCKET)
        return -E_NOT_SOCKET;

    if (msg->msg_namelen > NAME_MAX)
        return -E_NAME_TOO_LONG;

    if (msg->msg_iovlen <= 0 || msg->msg_iovlen > IOV_MAX)
        return -E_MSG_SIZE;

    // In connection-mode, specified address is ignored
    if (socket_mode_is_connection(socket->proto->type)) {
        if (socket->state != SOCKET_CONNECTED)
            return -E_NOT_CONNECTED;
    } else {
        if (socket->state != SOCKET_CONNECTED) {
            if (!msg->msg_name)
                return -E_DEST_ADDR_REQUIRED;

            err =socket->domain->verify_addr(msg->msg_name, msg->msg_namelen);
            if (err)
                return -err;
        }
    }

    return socket->proto->ops->sendmsg(socket, msg, flags);
}

/*
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/recvmsg.html
 */
static ssize_t socket_recvmsg(struct file *file, struct msghdr *msg, int flags)
{
    struct socket *socket = file->priv;

    if (file->vnode->type != VNODE_SOCKET)
        return -E_NOT_SOCKET;

    if (msg->msg_iovlen <= 0 || msg->msg_iovlen > IOV_MAX)
        return -E_MSG_SIZE;

    if (socket_mode_is_connection(socket->proto->type)) {
        if (socket->state != SOCKET_CONNECTED)
            return -E_NOT_CONNECTED;
    }

    return socket->proto->ops->recvmsg(socket, msg, flags);
}

static ssize_t socket_write(struct file *file, const char *data, size_t len)
{
    return file_send(file, data, len, 0);
}

static ssize_t socket_read(struct file *file, char *data, size_t len)
{
    return file_recv(file, data, len, 0);
}

static struct file_operations socket_fops = {
    .bind = socket_bind,
    .connect = socket_connect,
    .sendmsg = socket_sendmsg,
    .recvmsg = socket_recvmsg,
    .write = socket_write,
    .read = socket_read,
    .close = socket_close,
};
