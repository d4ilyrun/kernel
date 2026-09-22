/*
 * UDP protocol implementation.
 *
 * ## Locking
 *
 * * Modifying the local address of a UDP socket must be done while holding
 *   udp_sockets_lock.
 */

#define LOG_DOMAIN "udp"

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

/*
 * Hash comparison function for UDP sockets.
 *
 * Datagrams are identified by their local address/port tuple.
 */
static int udp_hash_compare(const void *entry_key, const void *key)
{
	const struct inet_sock *isock_entry = entry_key;
	const struct inet_sock *isock = key;

	if (isock->port != isock_entry->port)
		return !COMPARE_EQ;

	if (isock->addr == INADDR_ANY || isock_entry->addr == INADDR_ANY)
		return COMPARE_EQ;
	if (isock->addr == isock_entry->addr)
		return COMPARE_EQ;

	return !COMPARE_EQ;
}

/*
 * Hash functions for UDP sockets.
 */
static u32 udp_hash(const void *key)
{
	const struct inet_sock *isock = key;

	return hash32(isock->port << 16 | isock->port);
}

/*
 * Compute a UDP packet's checksum.
 */
static __be u16 udp_checksum(__be u32 daddr, __be u32 saddr,
			     __be u16 dport, __be u16 sport,
			     void *data, size_t data_len,
			     __be u16 checksum)
{
	struct pseudo_ipv4_header pseudo_ip;
	struct udp_header udp;
	struct iovec iovs[] = {
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

	return net_internet_checksum_vec(iovs, ARRAY_SIZE(iovs));
}

/*
 *
 */
error_t udp_receive_packet(struct packet *packet)
{
	struct ipv4_header *ip = packet->l3.ipv4;
	struct udp_header *udp = packet->l4.udp;
	size_t payload_size;
	struct udp_sock *usock;
	error_t err;

	packet_set_l4_size(packet, sizeof(struct udp_header));

	/* Compute packet's length without the UDP header. */
	payload_size = ntohs(udp->length);
	if (packet_payload_size(packet) < payload_size - sizeof(struct udp_header)) {
		err = E_INVAL;
		goto discard;
	}
	payload_size -= sizeof(struct udp_header);

	/* Check UDP checksum. */
	if (udp->checksum != 0 &&
	    udp_checksum(ip->daddr, ip->saddr, udp->dport, udp->sport, packet_payload(packet),
			 payload_size, udp->checksum)) {
		err = E_INVAL;
		goto discard;
	}

	/* Find destination socket. */
	locked_scope(&udp_sockets_lock) {
		struct hashtable_entry *entry;
		struct inet_sock key;

		key.addr = ip->daddr;
		key.port = udp->dport;
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
 * Bind UDP socket to a local address.
 */
static error_t af_inet_udp_bind(struct socket *socket, const struct sockaddr *addr, socklen_t len)
{
	struct udp_sock *usock = socket->data;
	struct sockaddr_in sin;
	struct inet_sock key;
	u16 port_start = INET_MAX_PRIVILEDGED_PORT + 1;
	u16 port_end = INET_MAX_PORT;
	error_t err;

	if (socket_is_bound(socket))
		return E_INVAL; /* rebinding not allowed. */

	memcpy(&sin, addr, sizeof(sin));
	if (sin.sin_port)
		port_end = port_start = ntohs(sin.sin_port);

	/*
	 * Find first available local address.
	 */
	err = E_ADDR_IN_USE;
	spinlock_acquire(&udp_sockets_lock);
	for (; port_start <= port_end; ++port_start) {
		key.addr = sin.sin_addr.s_addr;
		key.port = htons(port_start);
		if (hashtable_find(&udp_sockets, &key))
			continue;

		sin.sin_port = key.port;
		err = inet_sock_bind(&usock->isock, &sin);
		if (err) /* invalid local address */
			break;

		hashtable_insert(&udp_sockets, &usock->hash);
		socket->state |= SOCKET_BOUND;
		log_dbg("socket bound to " FMT_IP " port %d",
			 LOG_IP(usock->isock.addr),
			 htons(usock->isock.port));
		break;
	}
	spinlock_release(&udp_sockets_lock);

	return err;
}

/*
 * Set UDP socket peer address.
 */
static error_t af_inet_udp_connect(struct socket *socket, const struct sockaddr *addr, socklen_t len)
{
	struct udp_sock *usock = socket->data;
	error_t err;

	if (!socket_is_bound(socket)) {
		/*
		 * Socket has not already been bound, it must be bound to an unused address.
		 */
		struct sockaddr_in sin = {
			.sin_family = AF_INET,
			.sin_addr.s_addr = INADDR_ANY,
			.sin_port = 0,
		};

		err = af_inet_udp_bind(socket, (void *)&sin, sizeof(sin));
		if (err)
			return err;
	}

	err = inet_sock_connect(&usock->isock, (struct sockaddr_in *)addr);
	if (err)
		return err;

	socket->state |= SOCKET_CONNECTED;
	log_dbg("socket connected to " FMT_IP " port %d",
		 LOG_IP(usock->isock.route.dst.ip.sin_addr.s_addr),
		 htons(usock->isock.route.dst.ip.sin_port));

	return E_SUCCESS;
}

/*
 * Send one UDP datagram to a peer.
 */
static ssize_t af_inet_udp_send_one(struct socket *socket, const struct iovec *iov, int flags)
{
	struct udp_sock *usock = socket->data;
	struct net_route *route;
	struct udp_header udp;

	/* FIXME: Racy in case one changes the peer's address.
	 *        Possible solution would be to make isock->route a pointer to make
	 *        this operation RCU-friendly. */
	route = &usock->isock.route;
	udp.dport = route->dst.ip.sin_port;
	udp.sport = route->src.ip.sin_port;
	udp.length = ntohs(sizeof(struct udp_header) + iov->iov_len);
	udp.checksum = udp_checksum(route->dst.ip.sin_addr.s_addr, route->src.ip.sin_addr.s_addr,
				    udp.dport, udp.sport, iov->iov_base,
				    iov->iov_len, 0);

	return inet_sock_send_one(&usock->isock, IPPROTO_UDP, &udp, sizeof(struct udp_header), iov,
				  flags);
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
	usock->socket = socket;
	socket->data = usock;

	return E_SUCCESS;
}

/*
 *
 */
static void af_inet_udp_close(struct socket *socket)
{
	struct udp_sock *usock = socket->data;

	if (socket_is_bound(socket)) {
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
	hashtable_init(&udp_sockets, udp_hash, udp_hash_compare);

	return E_SUCCESS;
}

DECLARE_INITCALL(INIT_NORMAL, udp_init);
