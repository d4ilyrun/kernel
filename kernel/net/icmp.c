#define LOG_DOMAIN "icmp"

#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/net/ethernet.h>
#include <kernel/net/icmp.h>
#include <kernel/net/interface.h>
#include <kernel/net/ipv4.h>
#include <kernel/net/packet.h>
#include <kernel/net/route.h>

#include <string.h>

static u16 icmp_last_identifier = 0;

DECLARE_LLIST(af_inet_icmp_sockets);
DECLARE_SPINLOCK(af_inet_icmp_sockets_lock);

struct icmp_sock {
	struct inet_sock isock;
	struct socket *socket;
	u16 identifier; /** ICMP echo identifier */
	node_t this;
};

/***/
struct icmp_echo_header {
	struct icmp_header icmp;
	__be u16 identifier;
	__be u16 sequence;
};

#define to_isock(_isock) container_of(_isock, struct icmp_sock, this)

static error_t icmp_handle_echo_request(struct packet *packet)
{
	struct icmp_header *icmphdr = packet_payload(packet);
	struct packet *out_packet;
	struct net_route route;
	error_t ret = E_SUCCESS;

	/* Manually create out packet's routing table entry */
	route.netdev = packet->netdev;
	route.src.ip.sin_addr.s_addr = packet->l3.ipv4->daddr;
	memcpy(route.src.mac.mac_addr, packet->l2.ethernet->dst, sizeof(mac_address_t));
	memcpy(route.dst.mac.mac_addr, packet->l2.ethernet->src, sizeof(mac_address_t));
	route.dst.ip.sin_addr.s_addr = packet->l3.ipv4->saddr;

	/* Copy the request's content with a 'reply' type */
	icmphdr->type = ICMP_ECHO_REPLY;
	icmphdr->checksum = 0;
	icmphdr->checksum = net_internet_checksum(packet_payload(packet),
						  packet_payload_size(packet));

	out_packet = ipv4_build_packet(&route, IPPROTO_ICMP, NULL, 0, packet_payload(packet),
				       packet_payload_size(packet));
	packet_free(packet);

	if (IS_ERR(out_packet))
		return ERR_FROM_PTR(out_packet);

	ret = packet_send(out_packet);

	packet_free(out_packet);
	return ret;
}

/** Associate reply with the socket that sent the request */
static error_t icmp_handle_echo_reply(struct packet *packet)
{
	struct icmp_echo_header *icmphdr = packet_payload(packet);
	const struct icmp_sock *isock;

	if (packet_payload_size(packet) < sizeof(*icmphdr))
		return E_INVAL;

	locked_scope (&af_inet_icmp_sockets_lock) {
		FOREACH_LLIST (node, &af_inet_icmp_sockets) {
			isock = to_isock(node);
			if (isock->identifier > icmphdr->identifier) {
				packet_free(packet);
				return E_SUCCESS;
			}
			if (isock->identifier == icmphdr->identifier)
				break;
		}
	}

	return socket_enqueue_packet(isock->socket, packet);
}

error_t icmp_receive_packet(struct packet *packet)
{
	struct icmp_header *icmphdr = packet_payload(packet);
	error_t ret;

	/* NOTE: ICMP is a special case amongst IP protocols: the ICMP header is part of the
	 *       final payload (sent to and filled by the user). */
	packet_set_l4_size(packet, 0);
	if (net_internet_checksum(packet_payload(packet), packet_payload_size(packet))) {
		log_warn("invalid checksum");
		ret = E_INVAL;
		goto invalid_packet;
	}

	switch (icmphdr->type) {
	case ICMP_ECHO_REQUEST:
		return icmp_handle_echo_request(packet);
	case ICMP_ECHO_REPLY:
		return icmp_handle_echo_reply(packet);
	default:
		log_warn("unsupported packet type: %d", icmphdr->type);
		ret = E_NOT_SUPPORTED;
	}

invalid_packet:
	packet_free(packet);
	return ret;
}

static error_t
af_inet_ping_bind(struct socket *socket, const struct sockaddr *sockaddr, socklen_t len)
{
	struct icmp_sock *isock;
	error_t ret;

	socket_lock(socket);
	isock = socket->data;
	ret = inet_sock_bind(&isock->isock, (const struct sockaddr_in *)sockaddr);
	socket_unlock(socket);

	return ret;
}

static error_t
af_inet_ping_connect(struct socket *socket, const struct sockaddr *sockaddr, socklen_t len)
{
	struct icmp_sock *isock;
	struct sockaddr_in *dst = (struct sockaddr_in *)sockaddr;
	error_t ret;

	socket_lock(socket);
	isock = socket->data;

	ret = inet_sock_connect(&isock->isock, dst);
	if (ret)
		goto out;
	socket->state = SOCKET_CONNECTED;

out:
	socket_unlock(socket);
	return ret;
}

/*
 *
 */
static ssize_t af_inet_ping_send_one(struct socket *socket, const struct iovec *iov, int flags)
{
	struct icmp_sock *isock = socket->data;
	struct icmp_echo_header *icmphdr = iov->iov_base;

	if (iov->iov_len < sizeof(struct icmp_echo_header))
		return -E_INVAL;

	if (icmphdr->icmp.type != ICMP_ECHO_REQUEST)
		return -E_NOT_SUPPORTED;

	icmphdr->identifier = isock->identifier;
	icmphdr->icmp.checksum = 0;
	icmphdr->icmp.checksum = net_internet_checksum(iov->iov_base, iov->iov_len);

	return inet_sock_send_one(&isock->isock, socket->proto->proto, NULL, 0, iov, flags);
}

/*
 *
 */
static ssize_t af_inet_ping_sendmsg(struct socket *socket, const struct msghdr *msg, int flags)
{
	if (msg->msg_name) {
		not_implemented("overriding destination address in sendmsg");
		return -E_NOT_IMPLEMENTED;
	}

	return socket_dgram_sendmsg(socket, msg, flags, af_inet_ping_send_one);
}

/*
 *
 */
static error_t af_inet_ping_init(struct socket *socket)
{
	struct icmp_sock *isock;

	isock = kcalloc(1, sizeof(*isock), KMALLOC_KERNEL);
	if (isock == NULL)
		return E_NOMEM;

	inet_sock_init(&isock->isock);
	isock->identifier = icmp_last_identifier++;
	isock->socket = socket;
	socket->data = isock;

	spinlock_acquire(&af_inet_icmp_sockets_lock);
	llist_add_tail(&af_inet_icmp_sockets, &isock->this);
	spinlock_release(&af_inet_icmp_sockets_lock);

	return E_SUCCESS;
}

/*
 *
 */
static void af_inet_ping_close(struct socket *socket)
{
	struct icmp_sock *isock = socket->data;

	spinlock_acquire(&af_inet_icmp_sockets_lock);
	llist_add_tail(&af_inet_icmp_sockets, &isock->this);
	spinlock_release(&af_inet_icmp_sockets_lock);
}

static void af_inet_ping_release(struct socket *socket)
{
	kfree(socket->data);
}

struct socket_protocol_ops af_inet_icmp_ops = {
    .init = af_inet_ping_init,
    .close = af_inet_ping_close,
    .release = af_inet_ping_release,
    .bind = af_inet_ping_bind,
    .connect = af_inet_ping_connect,
    .sendmsg = af_inet_ping_sendmsg,
    .recvmsg = socket_dgram_recvmsg,
};
