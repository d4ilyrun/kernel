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

#include <kernel/devices/timer.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/socket.h>
#include <kernel/vfs.h>
#include <uapi/limits.h>

static struct file_operations socket_fops;

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
    struct stat *stat;

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

    stat = &vnode->stat;
    clock_get_time(&stat->st_mtim);
    stat->st_ctim = stat->st_mtim;
    stat->st_mode = S_IRWU | S_IRWG | S_IRWO;
    stat->st_nlink = 1;

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

static error_t
socket_bind(struct file *file, struct sockaddr *addr, socklen_t len)
{
    struct socket *socket = file->priv;

    if (socket->file->vnode->type != VNODE_SOCKET)
        return E_NOT_SOCKET;

    return socket->proto->ops->bind(socket, addr, len);
}

static error_t
socket_connect(struct file *file, struct sockaddr *addr, socklen_t len)
{
    struct socket *socket = file->priv;

    if (socket->file->vnode->type != VNODE_SOCKET)
        return E_NOT_SOCKET;

    return socket->proto->ops->connect(socket, addr, len);
}

/*
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/sendmsg.html
 */
static ssize_t
socket_sendmsg(struct file *file, const struct msghdr *msg, int flags)
{
    struct socket *socket = file->priv;

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
        if (!msg->msg_name && socket->state != SOCKET_CONNECTED)
            return -E_DEST_ADDR_REQUIRED;
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

    if (socket->state != SOCKET_CONNECTED && !msg->msg_name)
        return -E_NOT_CONNECTED;

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
};
