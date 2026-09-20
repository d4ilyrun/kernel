/*
 * TCP protocol implementation.
 *
 * ## Locking
 *
 * * Modifying the TCP socket's state (i.e. TCB, FSM) must
 *   be done while holding the socket's lock (socket_lock()).
 *
 * * Modifying the local address of a TCP socket, must be done while holding
 *   tcp_sockets_lock.
 *
 * ## References
 *
 * - RFC 9293 - Transmission Control Protocol (TCP)
 */

#include <dailyrun/net/ipv4.h>
#include <stddef.h>
#define LOG_DOMAIN "tcp"

#include <kernel/init.h>
#include <kernel/kmalloc.h>
#include <kernel/net/ipv4.h>
#include <kernel/net/tcp.h>
#include <kernel/net/packet.h>
#include <kernel/socket.h>

#include <libalgo/hashtable.h>
#include <libfsm.h>

#include <netinet/in.h>

/*
 * Hashtable containing all existing TCP sockets for both active (connect(),
 * accept()), and passive connections (listen()).
 */
static DECLARE_HASHTABLE(tcp_sockets, 256);
static DECLARE_SPINLOCK(tcp_sockets_lock);

/*
 * Hash comparison function for TCP sockets.
 *
 * Passive connections (in the LISTEN state) are, by definition, not connected
 * to a remote peer and are thus stored with the destination address set to
 * INADDR_ANY and the port set to 0.
 *
 * Active connections (created by a call to connect(), or returned by accept())
 * are necessarily connected to a remote peer and always contain destination
 * and local addresses different from INADDR_ANY.
 */
static int tcp_hash_compare(const void *entry_key, const void *key)
{
	const struct inet_sock *isock_entry = entry_key;
	const struct inet_sock *isock = key;
	const struct sockaddr_in *dst_entry = &isock_entry->route.dst.ip;
	const struct sockaddr_in *dst = &isock->route.dst.ip;

	/*
	 * Match socket's local address.
	 *
	 * INADDR_ANY is valid in this case and means 'any socket with
	 * the specified local port'.
	 */
	if (isock->port != isock_entry->port)
		return !COMPARE_EQ;
	if (isock_entry->addr != INADDR_ANY && isock->addr != INADDR_ANY &&
	    isock->addr != isock_entry->addr)
		return !COMPARE_EQ;

	/*
	 * Match connected peer's address.
	 */
	if (dst->sin_port != dst_entry->sin_port)
		return !COMPARE_EQ;
	if (dst->sin_addr.s_addr != dst_entry->sin_addr.s_addr)
		return !COMPARE_EQ;

	return COMPARE_EQ;
}

/*
 * Hash function for TCP sockets.
 */
static u32 tcp_hash(const void *key)
{
	const struct tcp_sock *tsock = key;
	const struct inet_sock *isock = &tsock->isock;

	return hash32(isock->port << 16 | isock->route.dst.ip.sin_port);
}

/*
 * Compute a TCP packet's checksum.
 */
static __be u16 tcp_checksum(__be u32 daddr, __be u32 saddr,
			     const struct tcp_header *header,
			     void *data, size_t data_len,
			     __be u16 checksum)
{
	struct pseudo_ipv4_header pseudo_ip;
	struct tcp_header tcp;
	struct iovec iovs[] = {
		{ .iov_base = &pseudo_ip, .iov_len = sizeof(pseudo_ip) },
		{ .iov_base = &tcp, .iov_len = tcp_header_size(header) },
		{ .iov_base = data, .iov_len = data_len },
	};

	pseudo_ip.daddr = daddr;
	pseudo_ip.saddr = saddr;
	pseudo_ip.proto = IPPROTO_TCP;
	pseudo_ip.zero = 0;
	pseudo_ip.proto_len = ntohs(sizeof(struct tcp_header) + data_len);

	memcpy(&tcp, header, sizeof(tcp));
	tcp.checksum = checksum;

	return net_internet_checksum_vec(iovs, ARRAY_SIZE(iovs));
}

/*
 * Compute the current initial sequence number.
 */
u32 tcp_compute_isn(void)
{
	/* TODO */
	return 1000;
}

/*
 * Append a segment to the transmitter's list of pending segments.
 */
static void tcp_add_pending_segment(struct tcp_sock *tsock, struct packet *packet)
{
	llist_add(&tsock->pending, &packet->tx_this);
	tsock->pending_bytes += tcp_segment_len(packet->l4.tcp, packet_payload_size(packet));
}

/*
 * Try and send as many pending segments as allowed by the current window.
 *
 * This function should be called when:
 * - Trying to send a new segment
 * - Moving the send window's right edge
 */
void tcp_send_pending_segments(struct tcp_sock *tsock)
{
	struct packet *packet;

	FOREACH_LLIST_ENTRY(packet, &tsock->pending, tx_this) {
		struct tcp_header *tcp = packet->l4.tcp;
		size_t seg_len = tcp_segment_len(tcp, packet_payload_size(packet));
		u32 seq;

		/* Check whether this segment can be transmitted inside
		 * the current window, and update the window if so.
		 *
		 * NOTE: Since we currently do not support packet segmentation
		 *       all segments inside the pending queue have the PSH
		 *       flag set. Because of this the SWS algorithm is reduced
		 *       to:
		 *           PUSHed and D <= U
		 *       D being the size of the current segment in our case.
		 *
		 * TODO: SWS algorithm once segmentation support is added.
		 */
		ASSERT(tcp->psh);
		if (tcp_window_send(&tsock->tcb, &seq, seg_len))
			break;
		tsock->pending_bytes -= seg_len;

		tcp->seq_num = htonl(seq);
		tcp->checksum = tcp_checksum(packet->l3.ipv4->daddr, packet->l3.ipv4->saddr, tcp,
					     packet_payload(packet), packet_payload_size(packet),
					     0);

		packet_send(packet);
	}
}

/*
 * Build a TCP segment.
 *
 * The control bits must be set inside the header prior to calling this function.
 */
static struct packet *tcp_build_segment(struct tcp_sock *tsock, struct tcp_header *tcp,
					void *payload, size_t payload_size)
{
	struct net_route *route;

	/* NOTE: The sequence number cannot be determined at this stage since SND.NXT
	 *       is only updated when it was deemed that the packet could be transmitted
	 *       (which is checked after building the packet). This field is thus filled
	 *       later by tcp_send_pending_segments(). The checksum also cannot be computed
	 *       either because of this.
	 */
	route = &tsock->isock.route;
	tcp->dport = route->dst.ip.sin_port;
	tcp->sport = route->src.ip.sin_port;
	tcp->data_offset = sizeof(*tcp) / sizeof(u32);

	return ipv4_build_packet(route, IPPROTO_TCP, tcp, tcp_header_size(tcp), payload,
				 payload_size);
}

/*
 *
 */
static error_t
tcp_send_segment(struct tcp_sock *tsock, struct tcp_header *tcp, void *payload, size_t payload_size)
{
	struct tcb *tcb = &tsock->tcb;
	struct net_route *route = &tsock->isock.route;
	struct packet *packet;
	size_t seg_len;
	size_t mss;

	/* No route to the remote peer. */
	if (WARN_ON(!(tsock->socket->state & (SOCKET_CONNECTING | SOCKET_CONNECTED))))
		return E_NOT_CONNECTED;

	packet = tcp_build_segment(tsock, tcp, payload, payload_size);
	if (IS_ERR(packet))
		return ERR_FROM_PTR(packet);

	seg_len = tcp_segment_len(tcp, packet_payload_size(packet));
	mss = MIN(tcb->send.mss + 20, route->dst.mtu);
	mss -= tcp_header_size(packet->l4.tcp);
	mss -= ipv4_header_option_size(packet->l3.ipv4);

	if (seg_len > mss) {
		/* TODO: TCP fragmentation */
		not_implemented("TCP fragmentation on TX path");
		packet_free(packet);
		return E_NOT_SUPPORTED;
	} else {
		/* PSH flag only set on the last packet */
		packet->l4.tcp->psh = true;
	}

	tcp_add_pending_segment(tsock, packet);
	tcp_send_pending_segments(tsock);

	return E_SUCCESS;
}

/*
 *
 */
error_t tcp_send_ack(struct tcp_sock *tsock)
{
	struct tcp_header tcp;

	memset(&tcp, 0, sizeof(tcp));
	tcp.ack = true;
	tcp.ack_num = htonl(tsock->tcb.recv.next);

	return tcp_send_segment(tsock, &tcp, NULL, 0);
}

/*
 *
 */
error_t tcp_send_rst(struct tcp_sock *tsock, unsigned int seq)
{
	struct tcp_header tcp;

	memset(&tcp, 0, sizeof(tcp));
	tcp.rst = true;
	tcp.ack_num = htonl(seq);

	return tcp_send_segment(tsock, &tcp, NULL, 0);
}

/*
 *
 */
error_t tcp_send_syn(struct tcp_sock *tsock, bool ack)
{
	struct tcp_header tcp;

	memset(&tcp, 0, sizeof(tcp));
	tcp.syn = true;
	tcp.ack = ack;
	tcp.ack_num = htonl(tsock->tcb.recv.next);

	return tcp_send_segment(tsock, &tcp, NULL, 0);
}

/*
 * Receive a TCP segment from the IP layer.
 *
 * @see 3.10.7 - SEGMENT ARRIVES
 */
error_t tcp_receive_packet(struct packet *packet)
{
	struct ipv4_header *ip = packet->l3.ipv4;
	struct tcp_header *seg = packet->l4.tcp;
	struct tcp_sock *tsock;
	struct hashtable_entry *entry;
	struct inet_sock key;
	struct tcp_fsm_ev ev;

	packet_set_l4_size(packet, tcp_header_size(seg));

	key.addr = ip->daddr;
	key.port = seg->dport;

	/* A TCP implementation MUST silently discard an incoming SYN segment
	 * that is addressed to a broadcast or multicast address. */
	if (seg->syn && (ipv4_is_broadcast(ip->daddr) || ipv4_is_multicast(ip->daddr)))
		goto discard;

	/* Discard segments containing an invalid checksum. */
	if (tcp_checksum(ip->daddr, ip->saddr, seg, packet_payload(packet),
			 packet_payload_size(packet->payload), seg->checksum))
		goto discard;

	/*
	 * Find the local socket associated with this connection.
	 */
	spinlock_acquire(&tcp_sockets_lock);
	key.route.dst.ip.sin_addr.s_addr = ip->saddr;
	key.route.dst.ip.sin_port = seg->sport;
	entry = hashtable_find(&tcp_sockets, &key);
	if (!entry) {
		/*
		 * No active connection found, find a passive one (i.e. listen()).
		 */
		key.route.dst.ip.sin_addr.s_addr = INADDR_ANY;
		key.route.dst.ip.sin_port = 0;
		entry = hashtable_find(&tcp_sockets, &key);
	}
	tsock = container_of(entry, struct tcp_sock, hash);
	spinlock_release(&tcp_sockets_lock);

	if (!entry) {
		/*
		 * 3.10.7.1 - SEGMENT ARRIVES (CLOSED)
		 */
		if (!seg->rst) {
			if (seg->ack) {
				/* TODO: <SEQ=0><ACK=SEG.SEQ+SEG.LEN><CTL=RST,ACK> */
			} else {
				/* TODO: <SEQ=SEG.ACK><CTL=RST>*/
			}
		}

		goto discard;
	}

	memset(&ev, 0, sizeof(ev));
	ev.type = TCP_EV_SEGMENT_ARRIVES;
	ev.seg = seg;
	ev.tsock = tsock;
	ev.seg_len = tcp_segment_len(seg, packet_payload_size(packet));
	socket_lock(tsock->socket);
	fsm_run(&tsock->fsm, &ev);
	socket_unlock(tsock->socket);

	return E_SUCCESS;

discard:
	packet_free(packet);
	return E_INVAL;
}

/*
 *
 */
static void tcp_timeout(void *data)
{
	struct tcp_sock *tsock = data;
	struct tcp_fsm_ev ev;

	memset(&ev, 0, sizeof(ev));
	ev.type =TCP_EV_TIMEOUT;

	fsm_run(&tsock->fsm, &ev);
}

/*
 * Send TCP segment to a peer.
 *
 * @see 3.10.2 - SEND Call
 */
static ssize_t af_inet_tcp_sendmsg(struct socket *socket, const struct msghdr *msg, int flags)
{
	struct tcp_sock *tsock;
	error_t err;

	socket_lock(socket);
	tsock = socket->data;

	switch (tsock->fsm.cur_state) {
	case TCP_LISTEN:
		err = E_NOT_SUPPORTED;
		goto fail;

	case TCP_SYN_SENT:
	case TCP_SYN_RECEIVED:
		/* Queue data to be sent once the connexion enters the ESTABLISHED state. */
		not_implemented("send() over unestablished connection");
		err = E_NOT_IMPLEMENTED;
		goto fail;

	case TCP_ESTABLISHED:
	case TCP_CLOSE_WAIT:
		not_implemented("send()");
		err = E_NOT_IMPLEMENTED;
		goto fail;

	case TCP_FIN_WAIT_1:
	case TCP_FIN_WAIT_2:
	case TCP_CLOSING:
	case TCP_LAST_ACK:
	case TCP_TIME_WAIT:
		/* Socket is being closed. */
		err = E_CONNEXION_RESET;
		goto fail;

	default:
		assert_not_reached();
	}

	socket_unlock(socket);
	return 0;
fail:
	socket_unlock(socket);
	return -err;
}

/*
 * Receive TCP segment from a peer.
 *
 * @see 3.10.3 - RECEIVE Call
 */
static ssize_t af_inet_tcp_recvmsg(struct socket *socket, struct msghdr *msg, int flags)
{
	struct tcp_sock *tsock;
	error_t err;

	socket_lock(socket);
	tsock = socket->data;

	switch (tsock->fsm.cur_state) {
	case TCP_LISTEN:
	case TCP_SYN_SENT:
	case TCP_SYN_RECEIVED:
		/* Queue data to be received once the connexion enters the ESTABLISHED state. */
		not_implemented("recv() from unestablished connection");
		err = E_NOT_IMPLEMENTED;
		goto fail;

	case TCP_ESTABLISHED:
	case TCP_FIN_WAIT_1:
	case TCP_FIN_WAIT_2:
		not_implemented("recv()");
		err = E_NOT_IMPLEMENTED;
		goto fail;

	case TCP_CLOSE_WAIT:
		not_implemented("recv() in CLOSE-WAIT state");
		err = E_NOT_IMPLEMENTED;
		goto fail;

	case TCP_CLOSING:
	case TCP_LAST_ACK:
	case TCP_TIME_WAIT:
		/* Socket is being closed. */
		err = E_CONNEXION_RESET;
		goto fail;

	default:
		assert_not_reached();
	}

	socket_unlock(socket);
	return 0;
fail:
	socket_unlock(socket);
	return -err;
}

/*
 * Close TCP connection.
 *
 * NOTE: Pending segments are not discarded during a close.
 *
 * @see 3.10.4 - CLOSE Call
 */
static void af_inet_tcp_close(struct socket *socket)
{
	struct tcp_sock *tsock;

	socket_lock(socket);
	tsock = socket->data;

	switch (tsock->fsm.cur_state) {
	case TCP_LISTEN:
	case TCP_SYN_SENT:
	case TCP_SYN_RECEIVED:
	case TCP_ESTABLISHED:
	case TCP_CLOSE_WAIT:
	case TCP_CLOSING:
	case TCP_LAST_ACK:
	case TCP_TIME_WAIT:
		not_implemented("close()");
		goto fail;

	case TCP_FIN_WAIT_1:
	case TCP_FIN_WAIT_2:
		goto fail;

	default:
		assert_not_reached();
	}

fail:
	socket_unlock(socket);
	return;
}

/*
 *
 */
static void af_inet_tcp_release(struct socket *socket)
{
	struct tcp_sock *tsock = socket->data;

	if (!tsock)
		return;
	if (tsock->time_wait_timeout)
		timeout_destroy(tsock->time_wait_timeout);
	kfree(tsock);
}

/*
 *
 */
static error_t af_inet_tcp_init(struct socket *socket)
{
	struct tcp_sock *tsock;

	tsock = kcalloc(1, sizeof(*tsock), KMALLOC_KERNEL);
	if (!tsock)
		return E_NOMEM;
	socket->data = tsock;

	tsock->time_wait_timeout = timeout_new(tcp_timeout, tsock);
	if (!tsock->time_wait_timeout)
		return E_NOMEM;

	inet_sock_init(&tsock->isock);
	fsm_init(&tsock->fsm, tcp_fsm_states, TCP_STATE_COUNT, TCP_CLOSED);
	tsock->socket = socket;
	tsock->hash.key = tsock;

	return E_SUCCESS;
}


struct socket_protocol_ops af_inet_tcp_ops = {
	.init = af_inet_tcp_init,
	.sendmsg = af_inet_tcp_sendmsg,
	.recvmsg = af_inet_tcp_recvmsg,
	.close = af_inet_tcp_close,
	.release = af_inet_tcp_release,
};

static error_t tcp_init(void)
{
	hashtable_init(&tcp_sockets, tcp_hash, tcp_hash_compare);

	return E_SUCCESS;
}

DECLARE_INITCALL(INIT_NORMAL, tcp_init);
