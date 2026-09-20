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
#include <kernel/syscalls.h>
#include <kernel/timer.h>
#include <kernel/vfs.h>

#include <limits.h>

static struct file_operations socket_fops;

static bool file_is_socket(const struct file *file)
{
	return file->vnode->type == VNODE_SOCKET;
}

static void socket_vnode_release(struct vnode *vnode)
{
	struct socket *socket = socket_from_vnode(vnode);
	struct packet *packet;
	struct packet *next;

	/*
	 * Discard any remaining received packet.
	 */
	spinlock_acquire(&socket->rx_lock);
	FOREACH_LLIST_ENTRY_SAFE(packet, next, &socket->rx_packets, rx_this) {
		llist_remove(&packet->rx_this);
		packet_free(packet);
	}
	spinlock_release(&socket->rx_lock);

	/*
	 * Flush connection backlog.
	 *
	 * Do it on release to wait for all in-process connections.
	 * They hold a reference to this socket and may insert entries
	 * into the backlog after the socket was closed.
	 */
	while (socket->conn_backlog_free < socket->conn_backlog_size) {
		struct socket_backlog_entry entry;

		if (socket_backlog_pop(socket, &entry, false))
			break;
		socket_put(entry.socket);
	}

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

	socket_lock(socket);

	/*
	 * Close connection and make socket unreachable.
	 */
	if (socket->proto->ops->close)
		socket->proto->ops->close(socket);

	socket_unlock(socket);
}

/*
 *
 */
static error_t socket_bind(struct file *file, const struct sockaddr *addr, socklen_t addr_len)
{
	struct socket *socket = file->priv;
	error_t err;

	if (!file_is_socket(file))
		return E_NOT_SOCKET;

	err = socket->domain->verify_addr(addr, addr_len);
	if (err)
		return err;

	socket_lock(socket);
	err = socket->proto->ops->bind(socket, addr, addr_len);
	socket_unlock(socket);
	return err;
}

/*
 *
 */
static error_t socket_connect(struct file *file, const struct sockaddr *addr, socklen_t addr_len)
{
	struct socket *socket = file->priv;
	error_t err;

	if (!file_is_socket(file))
		return E_NOT_SOCKET;

	if (addr->sa_family == AF_UNSPEC) {
		socket->state &= ~SOCKET_CONNECTED;
		return E_SUCCESS;
	}

	if (socket_mode_is_connection(socket->proto->type) && socket_is_connected(socket))
		return E_IS_CONNECTED;

	err = socket->domain->verify_addr(addr, addr_len);
	if (err)
		return err;

	socket_lock(socket);
	err = socket->proto->ops->connect(socket, addr, addr_len);
	socket_unlock(socket);
	return err;
}

/*
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/sendmsg.html
 */
static ssize_t socket_sendmsg(struct file *file, const struct msghdr *msg, int flags)
{
	struct socket *socket = file->priv;
	error_t err;

	if (!file_is_socket(file))
		return -E_NOT_SOCKET;

	if (msg->msg_namelen > NAME_MAX)
		return -E_NAME_TOO_LONG;

	if (msg->msg_iovlen <= 0 || msg->msg_iovlen > IOV_MAX)
		return -E_MSG_SIZE;

	// In connection-mode, specified address is ignored
	if (socket_mode_is_connection(socket->proto->type)) {
		if (!socket_is_connected(socket))
			return -E_NOT_CONNECTED;
	} else {
		if (!socket_is_connected(socket)) {
			if (!msg->msg_name)
				return -E_DEST_ADDR_REQUIRED;

			err = socket->domain->verify_addr(msg->msg_name, msg->msg_namelen);
			if (err)
				return err;
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

	if (!file_is_socket(file))
		return -E_NOT_SOCKET;

	if (msg->msg_iovlen <= 0 || msg->msg_iovlen > IOV_MAX)
		return -E_MSG_SIZE;

	if (socket_mode_is_connection(socket->proto->type) && !socket_is_connected(socket))
		return -E_NOT_CONNECTED;

	return socket->proto->ops->recvmsg(socket, msg, flags);
}

/*
 *
 */
static ssize_t socket_write(struct file *file, const char *data, size_t len)
{
	struct iovec iov = {
	    .iov_base = (void *)data,
	    .iov_len = len,
	};
	struct msghdr msg = {
	    .msg_iov = &iov,
	    .msg_iovlen = 1,
	};

	return socket_sendmsg(file, &msg, 0);
}

/*
 *
 */
static ssize_t socket_read(struct file *file, char *data, size_t len)
{
	struct iovec iov = {
	    .iov_base = data,
	    .iov_len = len,
	};
	struct msghdr msg = {
	    .msg_iov = &iov,
	    .msg_iovlen = 1,
	};

	return socket_recvmsg(file, &msg, 0);
}

static struct file_operations socket_fops = {
    .bind = socket_bind,
    .connect = socket_connect,
    .write = socket_write,
    .read = socket_read,
    .close = socket_close,
};

/*
 *
 */
static error_t socket_listen(struct socket *socket, int backlog_size)
{
	struct socket_backlog_entry *backlog;
	error_t err;

	backlog = kcalloc(backlog_size, sizeof(*backlog), KMALLOC_KERNEL);
	if (!backlog)
		return E_NOMEM;

	socket_lock(socket);

	if (!socket->proto->ops->listen) {
		err = E_NOT_SUPPORTED;
		goto out;
	}
	if (socket_is_listening(socket) || socket_is_connected(socket)) {
		err = E_INVAL;
		goto out;
	}

	ringbuffer_init(&socket->conn_backlog, backlog, backlog_size * sizeof(*backlog));
	socket->conn_backlog_size = backlog_size;
	socket->conn_backlog_free = backlog_size;

	err = socket->proto->ops->listen(socket);
	if (err)
		goto out;

	socket_unlock(socket);
	return E_SUCCESS;
out:
	ringbuffer_init(&socket->conn_backlog, NULL, 0);
	socket_unlock(socket);
	kfree(backlog);
	return err;
}

/*
 *
 */
static struct socket *
socket_accept(struct socket *socket, struct sockaddr *sockaddr, socklen_t *salen, bool nonblock)
{
	struct socket_backlog_entry entry;
	error_t err = E_SUCCESS;

	socket_lock(socket);

	if (!socket_is_listening(socket)) {
		err = E_INVAL;
		goto fail;
	}

	err = socket_backlog_pop(socket, &entry, nonblock);
	if (err)
		goto fail;

	if (sockaddr && salen) {
		*salen = MIN(*salen, entry.salen);
		memcpy(sockaddr, &entry.addr, *salen);
	}

	/* Reference to the socket inherited from the backlog. */
	return entry.socket;

fail:
	socket_unlock(socket);
	return PTR_ERR(err);
}

/*
 *
 */
int sys_socket(int domain, int type, int proto)
{
	struct socket *socket;
	struct file *file = NULL;
	struct vnode *vnode = NULL;
	error_t err;
	int fd;

	/* NOTE: socket_alloc() increments the socket's refcount to 1. */
	socket = socket_alloc();
	if (!socket)
		return -E_NOMEM;
	vnode = socket_vnode(socket);

	err = socket_init(socket, domain, type, proto);
	if (err)
		goto fail;

	file = file_open(vnode, &socket_fops);
	if (IS_ERR(file)) {
		log_err("Failed to open socket file: %pE", file);
		err = ERR_FROM_PTR(file);
		file = NULL;
		goto fail;
	}
	file->priv = socket;

	fd = process_add_fd(current->process, file, FD_RW);
	if (fd < 0) {
		err = -fd;
		goto fail;
	}

	return fd;

fail:
	if (file)
		file_put(file);
	socket_put(socket);
	return -err;
}

/*
 *
 */
int sys_connect(int fd, const struct sockaddr *addr, socklen_t addr_len)
{
	struct fd *fdp;
	error_t err;

	fdp = process_fd_get(current->process, fd);
	if (!fdp)
		return -E_BAD_FD;

	err = socket_connect(fdp->file, addr, addr_len);
	process_fd_put(current->process, fdp);

	return -err;
}

/*
 *
 */
int sys_bind(int fd, const struct sockaddr *addr, socklen_t addr_len)
{
	struct fd *fdp;
	error_t err;

	fdp = process_fd_get(current->process, fd);
	if (!fdp)
		return -E_BAD_FD;

	err = socket_bind(fdp->file, addr, addr_len);
	process_fd_put(current->process, fdp);

	return -err;
}

/*
 *
 */
ssize_t sys_sendmsg(int fd, const struct msghdr *msg_in, int flags)
{
	struct msghdr msg;
	struct fd *fdp;
	ssize_t count;

	fdp = process_fd_get(current->process, fd);
	if (!fdp)
		return -E_BAD_FD;

	memcpy(&msg, &msg_in, sizeof(msg));
	msg.msg_flags = flags;
	flags = fdp->flags;

	count = socket_sendmsg(fdp->file, &msg, flags);
	process_fd_put(current->process, fdp);

	return count;
}

/*
 *
 */
ssize_t sys_recvmsg(int fd, struct msghdr *msg_in, int flags)
{
	struct msghdr msg;
	struct fd *fdp;
	ssize_t count;

	fdp = process_fd_get(current->process, fd);
	if (!fdp)
		return -E_BAD_FD;

	memcpy(&msg, &msg_in, sizeof(msg));
	msg.msg_flags = flags;
	flags = fdp->flags;

	count = socket_recvmsg(fdp->file, &msg, flags);
	process_fd_put(current->process, fdp);

	msg_in->msg_flags = msg.msg_flags;

	return count;
}

/*
 * Listen syscall.
 */
int sys_listen(int fd, int backlog)
{
	struct fd *fdp;
	struct socket *socket;
	error_t err;

	fdp = process_fd_get(current->process, fd);
	if (!fdp)
		return -E_BAD_FD;
	if (!file_is_socket(fdp->file)) {
		err = E_NOT_SOCKET;
		goto out;
	}

	socket = fdp->file->priv;
	err = socket_listen(socket, backlog);

out:
	process_fd_put(current->process, fdp);
	return -err;
}


/*
 * Accept syscall.
 */
int sys_accept(int fd, struct sockaddr *saddr, socklen_t *salen)
{
	struct process *proc = current->process;
	int new_fd;
	struct fd *fdp;
	struct fd *new_fdp;
	struct socket *socket;
	struct socket *new_socket;
	struct file *new_file;
	error_t err;

	fdp = process_fd_get(proc, fd);
	if (!fdp)
		return -E_BAD_FD;
	if (!file_is_socket(fdp->file)) {
		err = E_NOT_SOCKET;
		goto out;
	}

	/* Reserve a file descriptor.
	 *
	 * FIXME: There is a slight race condition if a thread were to perform a syscall
	 *        on the reserved FD while it still has no attached file. What would result
	 *        in an EBADFD will now panic. We may want to rethink this.
	 */
	new_fd = process_add_fd(proc, NULL, 0);
	if (new_fd < 0) {
		err = -new_fd;
		goto out;
	}

	socket = fdp->file->priv;
	new_socket = socket_accept(socket, saddr, salen, false);
	if (IS_ERR(new_socket)) {
		err = ERR_FROM_PTR(new_socket);
		process_remove_fd(proc, new_fd);
		goto out;
	}

	new_file = file_open(socket_vnode(socket), &socket_fops);
	if (IS_ERR(new_file)) {
		log_err("Failed to open socket file: %pE", new_file);
		err = ERR_FROM_PTR(new_file);
		socket_put(new_socket); /* TODO: We should re-insert it instead. */
		process_remove_fd(proc, new_fd);
		goto out;
	}

	new_fdp = process_fd_get(proc, new_fd);
	new_file->priv = socket;
	new_fdp->file = new_file;
	process_fd_put(proc, new_fdp);

	err = E_SUCCESS;
out:
	process_fd_put(proc, fdp);
	return -err;
}
