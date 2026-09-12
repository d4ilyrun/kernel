/*
 * UDP protocol implementation.
 *
 * ## Locking
 *
 * * Modifying the local address of a UDP socket must be done while holding
 *   udp_sockets_lock.
 */

#include <kernel/init.h>
#include <kernel/kmalloc.h>
#include <kernel/net/ipv4.h>
#include <kernel/net/udp.h>
#include <kernel/socket.h>

#include <libalgo/hashtable.h>

#include <netinet/in.h>

static DECLARE_HASHTABLE(udp_sockets, 256);
static DECLARE_SPINLOCK(udp_sockets_lock);

/*
 * Private UDP socket data.
 */
struct udp_sock {
	struct inet_sock isock;
	struct socket *socket;
	struct hashtable_entry hash;
};

static inline bool udp_sock_is_bound(const struct udp_sock *usock)
{
	return usock->isock.port != 0;
}

/*
 * Compute a UDP packet's checksum.
 */
static __be u16 udp_checksum(__be u32 saddr, __be u32 daddr,
			     __be u16 sport, __be u16 dport,
			     void *data, size_t data_len,
			     __be u16 checksum)
{
	struct pseudo_ipv4_header pseudo_ip;
	struct udp_header udp;
	struct iovec iovs[4] = {
		{ .iov_base = &pseudo_ip, .iov_len = sizeof(pseudo_ip) },
		{ .iov_base = &udp, .iov_len = sizeof(udp) },
		{ .iov_base = data, .iov_len = data_len },
	};

	pseudo_ip.daddr = daddr;
	pseudo_ip.saddr = saddr;
	pseudo_ip.proto = IPPROTO_UDP;
	pseudo_ip.zero = 0;
	pseudo_ip.proto_len = ntohs(sizeof(struct udp_header) + data_len);

	udp.dport = dport;
	udp.sport = sport;
	udp.length = ntohs(sizeof(struct udp_header) + data_len);
	udp.checksum = checksum;

	return net_internet_checksum_vec(iovs, 4);
}

/*
 *
 */
error_t udp_receive_packet(struct packet *packet)
{
	struct ipv4_header *ip = packet->l3.ipv4;
	struct udp_header *udp = packet->l4.udp;
	struct udp_sock *usock;
	struct inet_sock key;
	error_t err;

	packet_set_l4_size(packet, sizeof(struct udp_header));

	/*
	 * Check UDP checksum.
	 */
	if (udp->checksum != 0) {
		if (udp_checksum(ip->daddr, ip->saddr, udp->dport, udp->sport,
				 packet_payload(packet), packet_payload_size(packet),
				 udp->checksum)) {
			err = E_INVAL;
			goto discard;
		}
	}

	/*
	 * Find destination socket.
	 */
	locked_scope(&udp_sockets_lock) {
		struct hashtable_entry *entry;

		key.port = udp->dport;
		key.addr = ip->daddr;
		entry = hashtable_find(&udp_sockets, &key);
		if (!entry) {
			err = E_NOENT;
			goto discard;
		}
		usock = entry->key;
	}

	err = socket_enqueue_packet(usock->socket, packet);
	if (err)
		goto discard;

	return E_SUCCESS;

discard:
	packet_free(packet);
	return err;
}

/*
 * Bind the UDP socket to a local address.
 */
static error_t af_inet_udp_bind(struct socket *socket, const struct sockaddr *addr, socklen_t len)
{
	struct udp_sock *usock;
	struct sockaddr_in sin;
	struct inet_sock key;
	u16 port_start = INET_MIN_PORT;
	u16 port_end = INET_MAX_PORT;
	error_t err;

	socket_lock(socket);
	usock = socket->data;

	if (udp_sock_is_bound(usock)) {
		/* rebinding not allowed. */
		err = E_INVAL;
		goto out;
	}

	memcpy(&sin, addr, sizeof(sin));
	if (sin.sin_port)
		port_end = port_start = ntohs(sin.sin_port);

	/*
	 * Find first available local address.
	 */
	err = E_ADDR_IN_USE;
	spinlock_acquire(&udp_sockets_lock);
	while (port_start <= port_end) {
		key.addr = sin.sin_addr.s_addr;
		key.port = htons(port_start);
		if (!hashtable_find(&udp_sockets, &key)) {
			sin.sin_port = key.port;
			err = inet_sock_bind(&usock->isock, &sin);
			if (!err) /* invalid local address */
				hashtable_insert(&udp_sockets, &usock->hash);
			break;
		}
		port_start += 1;
	}
	spinlock_release(&udp_sockets_lock);

out:
	socket_unlock(socket);
	return err;
}

/*
 * Set UDP socket peer address.
 */
static error_t af_inet_udp_connect(struct socket *socket, const struct sockaddr *addr, socklen_t len)
{
	struct udp_sock *usock;
	error_t err;

	socket_lock(socket);
	usock = socket->data;
	err = inet_sock_connect(&usock->isock, (struct sockaddr_in *)addr);
	if (err)
		goto out;
	socket->state = SOCKET_CONNECTED;
out:
	socket_unlock(socket);
	return err;
}

/*
 * Send one UDP datagram to a peer.
 */
static ssize_t af_inet_udp_send_one(struct socket *socket, const struct iovec *iov, int flags)
{
	struct udp_sock *usock = socket->data;
	struct net_route *route;
	struct udp_header udp;

	if (iov->iov_len < sizeof(struct udp_header))
		return -E_INVAL;

	/* FIXME: Racy in case one changes the connected address.
	 *        Possible solution would be to make isock->route a pointer to make
	 *        this operation RCU-friendly. */
	route = &usock->isock.route;
	udp.dport = route->dst.ip.sin_port;
	udp.sport = route->src.ip.sin_port;
	udp.length = ntohs(sizeof(struct udp_header) + iov->iov_len);
	udp.checksum = udp_checksum(route->dst.ip.sin_addr.s_addr, route->src.ip.sin_addr.s_addr,
				    route->dst.ip.sin_port, route->dst.ip.sin_port, iov->iov_base,
				    iov->iov_len, 0);

	return inet_sock_send_one(&usock->isock, IPPROTO_UDP, &udp, sizeof(struct udp_header),
				  iov->iov_base, iov->iov_len);
}

/*
 * Send UDP datagrams to a peer.
 */
static ssize_t af_inet_udp_sendmsg(struct socket *socket, const struct msghdr *msg, int flags)
{
	if (msg->msg_name) {
		not_implemented("overriding destination address in sendmsg");
		return -E_NOT_IMPLEMENTED;
	}

	return socket_dgram_sendmsg(socket, msg, flags, af_inet_udp_send_one);
}

/*
 *
 */
static error_t af_inet_udp_init(struct socket *socket)
{
	struct udp_sock *usock;

	usock = kcalloc(1, sizeof(*usock), KMALLOC_KERNEL);
	if (!usock)
		return E_NOMEM;

	inet_sock_init(&usock->isock);
	usock->hash.key = &usock->isock;
	socket->data = usock;

	return E_SUCCESS;
}

/*
 *
 */
static void af_inet_udp_close(struct socket *socket)
{
	struct udp_sock *usock = socket->data;

	if (udp_sock_is_bound(usock)) {
		/* socket was bound to a local port */
		spinlock_acquire(&udp_sockets_lock);
		hashtable_remove(&udp_sockets, &usock->hash.key);
		spinlock_release(&udp_sockets_lock);
	}
}

/*
 *
 */
static void af_inet_udp_release(struct socket *socket)
{
	if (socket->data) {
		kfree(socket->data);
		socket->data = NULL;
	}
}

struct socket_protocol_ops af_inet_udp_ops = {
    .init = af_inet_udp_init,
    .close = af_inet_udp_close,
    .release = af_inet_udp_release,
    .bind = af_inet_udp_bind,
    .connect = af_inet_udp_connect,
    .sendmsg = af_inet_udp_sendmsg,
    .recvmsg = socket_dgram_recvmsg,
};

static error_t udp_init(void)
{
	hashtable_init(&udp_sockets, inet_sock_hash, inet_sock_hash_compare);
	return E_SUCCESS;
}

DECLARE_INITCALL(INIT_NORMAL, udp_init);
