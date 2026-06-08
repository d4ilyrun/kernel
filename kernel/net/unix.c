#define LOG_DOMAIN "unix"

#include <kernel/init.h>
#include <kernel/kmalloc.h>
#include <kernel/net/packet.h>
#include <kernel/socket.h>
#include <kernel/spinlock.h>

#include <libalgo/linked_list.h>

#include <limits.h>
#include <string.h>
#include <sys/un.h>

static DECLARE_LLIST(usocks);
static DECLARE_SPINLOCK(usocks_lock);

/*
 * UNIX socket private data.
 */
struct usock {
    struct socket *socket;
    struct usock *peer;
    char path[PATH_MAX];
    LLIST_NODE(this);
};

static struct usock *usock_alloc(void)
{
    return kcalloc(1, sizeof(struct usock), KMALLOC_KERNEL);
}

static void usock_free(struct usock *usock)
{
    kfree(usock);
}

/* Find an existing UNIX socket
 *
 * NOTE: If a socket is found, its reference count is increased. The caller
 *       is responsible for releasing this extra reference.
 */
static struct usock *usock_get_by_path(const char *path)
{
    struct usock *usock;

    WARN_ON(!spinlock_is_held(&usocks_lock));

    FOREACH_LLIST_ENTRY (usock, &usocks, this) {
        if (!strncmp(usock->path, path, PATH_MAX)) {
            socket_get(usock->socket);
            return usock;
        }
    }

    return NULL;
}

/*
 *
 */
static error_t af_unix_bind(struct socket *socket, const struct sockaddr *addr,
                            socklen_t addr_len)
{
    struct sockaddr_un *sun = (struct sockaddr_un *)addr;
    struct usock *usock;
    error_t err = E_SUCCESS;

    UNUSED(addr_len);

    spinlock_acquire(&usocks_lock);
    usock = usock_get_by_path(sun->sun_path);
    if (usock) {
        socket_put(usock->socket);
        err = E_ADDR_IN_USE;
        goto out;
    }

    socket_lock(socket);
    usock = socket->data;
    strlcpy(usock->path, sun->sun_path, PATH_MAX);
    llist_remove(&usock->this);
    llist_add(&usocks, &usock->this);
    socket_unlock(socket);

out:
    spinlock_release(&usocks_lock);
    return err;
}

/*
 *
 */
static error_t af_unix_connect(struct socket *socket,
                               const struct sockaddr *addr, socklen_t addr_len)
{
    struct sockaddr_un *sun = (struct sockaddr_un *)addr;
    struct usock *usock = socket->data;
    struct usock *peer;

    UNUSED(addr_len);

    spinlock_acquire(&usocks_lock);
    peer = usock_get_by_path(sun->sun_path);
    spinlock_release(&usocks_lock);

    if (!peer)
        return E_ADDR_NOT_AVAILABLE;

    if (peer->socket->proto != socket->proto) {
        socket_put(peer->socket);
        return E_PROTO_TYPE;
    }

    socket_lock(socket);
    usock->peer = peer;
    usock->socket->state = SOCKET_CONNECTED;
    socket_unlock(socket);

    return E_SUCCESS;
}

/*
 *
 */
static ssize_t
af_unix_send_one(struct socket *socket, const struct iovec *iov, int flags)
{
    struct usock *usock = socket->data;
    struct usock *peer = usock->peer;
    struct packet *packet;
    error_t error;

    UNUSED(flags);

    WARN_ON(!spinlock_is_held(&socket->lock));

    /* FIXME: Sanitize input, and limit the buffer size.
     *        This is a crazy thing to do.
     */
    packet = packet_new(iov->iov_len);
    if (IS_ERR(packet)) {
        log_err("failed to allocate packet: %pE", packet);
        return -ERR_FROM_PTR(packet);
    }

    /* unix packets don't contain a header */
    packet_set_l2_size(packet, 0);
    packet_set_l3_size(packet, 0);
    packet_put(packet, iov->iov_base, iov->iov_len);

    error = socket_enqueue_packet(peer->socket, packet);
    return error ? -error : iov->iov_len;
}

/*
 *
 */
static ssize_t af_unix_sendmsg(struct socket *socket,
                               const struct msghdr *msg,
                               int flags)
{
    if (msg->msg_name) {
        not_implemented("overriding destination address in sendmsg");
        return -E_NOT_IMPLEMENTED;
    }

    return socket_dgram_sendmsg(socket, msg, flags, af_unix_send_one);
}

/*
 *
 */
static void af_unix_close(struct socket *socket)
{
    struct usock *usock = socket->data;

    if (usock->peer) {
        socket_put(usock->peer->socket);
        usock->peer = NULL;
    }

    spinlock_acquire(&usocks_lock);
    llist_remove(&usock->this);
    spinlock_release(&usocks_lock);
}

/*
 *
 */
static void af_unix_release(struct socket *socket)
{
    usock_free(socket->data);
}

static const struct socket_protocol af_unix_dgram = {
    .type = SOCK_DGRAM,
    .proto = 0,
    .ops = &(struct socket_protocol_ops) {
        .close = af_unix_close,
        .release = af_unix_release,
        .bind = af_unix_bind,
        .connect = af_unix_connect,
        .sendmsg = af_unix_sendmsg,
        .recvmsg = socket_dgram_recvmsg,
    },
};

/*
 *
 */
static error_t af_unix_socket_init(struct socket *socket, int type, int proto)
{
    struct usock *usock;
    const struct socket_protocol *sock_proto;

    UNUSED(proto);
    switch (type) {
    case SOCK_DGRAM:
        sock_proto = &af_unix_dgram;
        break;
    case SOCK_STREAM:
        return E_NOT_SUPPORTED;
    default:
        return E_INVAL;
    }

    usock = usock_alloc();
    if (!usock)
        return E_NOMEM;

    usock->socket = socket;
    socket->data = usock;
    socket->proto = sock_proto;
    INIT_LLIST_NODE(usock->this);

    return E_SUCCESS;
}

/*
 *
 */
static error_t af_unix_verify_addr(const struct sockaddr *addr,
                                   socklen_t addr_len)
{
    struct sockaddr_un *sun = (struct sockaddr_un *)addr;

    if (sun->sun_family != AF_UNIX)
        return E_INVAL;

    if (addr_len > sizeof(struct sockaddr_un) ||
        addr_len <= offsetof(struct sockaddr_un, sun_path))
        return E_INVAL;

    if (sun->sun_path[0] == 0)
        return E_NOENT;

    return E_SUCCESS;
}

static struct socket_domain af_unix = {
    .domain = AF_UNIX,
    .socket_init = af_unix_socket_init,
    .verify_addr = af_unix_verify_addr,
};

static error_t unix_init(void)
{
    error_t err;

    err = socket_domain_register(&af_unix);
    if (err)
        PANIC("failed to register AF_UNIX domain: %pE", &err);

    log_info("Registered AF_UNIX domain");

    return err;
}

DECLARE_INITCALL(INIT_NORMAL, unix_init);
