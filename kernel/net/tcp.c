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
 * ## TODO
 *
 * - Retransmission
 * - Mandatory congestion algorithm
 *
 * ## References
 *
 * - RFC 9293 - Transmission Control Protocol (TCP)
 */

#define LOG_DOMAIN "tcp"

#include <kernel/init.h>
#include <kernel/kmalloc.h>
#include <kernel/net/ipv4.h>
#include <kernel/net/tcp.h>
#include <kernel/net/packet.h>
#include <kernel/socket.h>
#include <kernel/timer.h>

#include <libalgo/hashtable.h>
#include <libfsm.h>

#include <netinet/in.h>

/*
 * Hashtable containing all existing TCP sockets for both active (connect(),
 * accept()), and passive connections (listen()).
 */
tcp_sockets_hashtable_t tcp_sockets;
DECLARE_SPINLOCK(tcp_sockets_lock);

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
			     void *data, size_t data_len)
{
	struct pseudo_ipv4_header pseudo_ip;
	struct iovec iovs[] = {
		{ .iov_base = &pseudo_ip, .iov_len = sizeof(pseudo_ip) },
		{ .iov_base = (void *)header, .iov_len = tcp_header_size(header) },
		{ .iov_base = data, .iov_len = data_len },
	};

	pseudo_ip.daddr = daddr;
	pseudo_ip.saddr = saddr;
	pseudo_ip.proto = IPPROTO_TCP;
	pseudo_ip.zero = 0;
	pseudo_ip.proto_len = htons(tcp_header_size(header) + data_len);

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
 * Compute the current initial sequence number.
 */
void tcp_set_send_mss(struct tcp_sock *tsock, u16 mss)
{
	if (mss < TCP_IPV4_MSS)
		mss = TCP_IPV4_MSS;
	tsock->tcb.send.mss = mss;
}

/*
 * Compute the segment's maximum size.
 */
unsigned int tcp_effective_mss(const struct tcp_sock *tsock, struct tcp_header *seg)
{
	const struct net_route *route = &tsock->isock.route;
	const struct tcb *tcb = &tsock->tcb;
	unsigned int mss;

	mss = MIN(tcb->send.mss + sizeof(struct tcp_header),
		  route->dst.mtu - sizeof(struct ipv4_header));
	mss -= tcp_header_size(seg);
	mss -= 0; /* IP options */

	return mss;
}

/*
 * Add packet to the socket's transmission queue.
 *
 * Packets stay inside this queue as long as they haven't been fully acknowledged
 * by the peer.
 *
 * TODO: Segment retransmission
 */
static void tcp_transmit_queue_add(struct tcp_sock *tsock, struct packet *packet)
{
	llist_add_tail(&tsock->xmit_queue, &packet->tx_this);
	packet_send(packet);
}

/*
 * Pop all packets from the transmit queue whose sequence numbers have all been acnkowledged
 * by the caller.
 *
 * NOTE: This function does not verify that the sequence number is valid (i.e inside the current
 *       send window), this should be done by the caller by calling tcp_window_accept_ack().
 */
void tcp_transmit_queue_ack(struct tcp_sock *tsock, u32 ack)
{
	struct packet *packet;
	struct packet *next;

	FOREACH_LLIST_ENTRY_SAFE(packet, next, &tsock->xmit_queue, tx_this) {
		struct tcp_header *seg = packet->l4.tcp;
		size_t seg_len = tcp_segment_len(seg, packet_payload_size(packet));
		u32 seq = ntohl(seg->seq_num);

		/* check if packet still has unacknowledged bytes */
		if (ack - seq < seg_len)
			break;

		llist_remove(&packet->tx_this);
		packet_free(packet);
	}
}

/*
 * Pop all packets from the transmit queue.
 */
void tcp_transmit_queue_flush(struct tcp_sock *tsock)
{
	struct packet *packet;
	struct packet *next;

	FOREACH_LLIST_ENTRY_SAFE(packet, next, &tsock->xmit_queue, tx_this) {
		llist_remove(&packet->tx_this);
		packet_free(packet);
	}
}

static void tcp_log_tcb(const struct tcb *tcb)
{
	struct tcp_sock *tsock = container_of(tcb, struct tcp_sock, tcb);

	log_dbg("TCB {%p4:%u}", &tsock->isock.addr, ntohs(tsock->isock.port));
	log_dbg("- SND -");
	log_dbg(" SND.UNA \t%u", tcb->send.unack);
	log_dbg(" SND.NXT \t%u", tcb->send.next);
	log_dbg(" SND.WND \t%u", tcb->send.window_size);
	log_dbg(" SND.WL1 \t%u", tcb->send.window_l1);
	log_dbg(" SND.WL2 \t%u", tcb->send.window_l2);
	log_dbg(" SND.MSS \t%u", tcb->send.mss);
	log_dbg("- RCV -");
	log_dbg(" RCV.NXT \t%u", tcb->recv.next);
	log_dbg(" RCV.WND \t%u", tcb->recv.window_size);
	log_dbg(" RCV.BUFF \t%u", tcb->recv.buff);
	log_dbg(" RCV.USER \t%u", tcb->recv.user);
	log_dbg("---");
}

/*
 * Build a TCP segment and add it to the transmit queue.
 *
 * The control bits must be set inside the header prior to calling this function.
 */
static bool
tcp_transmit_segment(struct tcp_sock *tsock, struct net_route *route, struct packet *packet)
{
	struct tcb *tcb = &tsock->tcb;
	struct tcp_header *seg = packet->l4.tcp;
	size_t seg_len = tcp_segment_len(seg, packet_payload_size(packet));
	u32 seq;

	if (!seg->rst) {
		/* SEQ holds a special meaning in the case of RST segments. */
		if (!tcp_window_send(&tsock->tcb, &seq, seg_len)) {
			tcp_log_tcb(&tsock->tcb);
			return false;
		}
	} else
		ASSERT(packet_payload_size(packet) == 0);

	if (seg->fin) {
		/* It is illegal to send a second FIN flag. */
		ASSERT(!tcb->fin_sent);
		tcb->fin_sent = true;
		tcb->fin_seq_num = seq + seg_len - 1;
	}

	seg->dport = route->dst.ip.sin_port;
	seg->sport = route->src.ip.sin_port;
	seg->seq_num = htonl(seq);
	seg->window = htons(tsock->tcb.recv.window_size);
	seg->checksum = 0;
	seg->checksum = tcp_checksum(packet->l3.ipv4->daddr, packet->l3.ipv4->saddr, seg,
				     packet_payload(packet), packet_payload_size(packet));

	tcp_transmit_queue_add(tsock, packet);

	return true;
}

/*
 *
 */
static error_t tcp_send_control(struct tcp_sock *tsock, struct tcp_header *tcphdr,
				size_t tcphdr_size)
{
	struct net_route *route;
	struct packet *packet;

	/* No route to the peer. */
	if (WARN_ON(!(tsock->socket->state & (SOCKET_CONNECTING | SOCKET_CONNECTED))))
		return E_NOT_CONNECTED;

	if (tcphdr_size % sizeof(u32)) {
		WARN("invalid header size, removing options");
		tcphdr_size = sizeof(struct tcp_header);
	}
	tcphdr->data_offset = tcphdr_size / sizeof(u32);

	route = &tsock->isock.route;
	packet = ipv4_build_packet(route, IPPROTO_TCP, tcphdr, tcp_header_size(tcphdr), NULL, 0);
	if (IS_ERR(packet))
		return ERR_FROM_PTR(packet);

	if (!tcp_transmit_segment(tsock, route, packet)) {
		packet_free(packet);
		return E_NO_BUFFER_SPACE;
	}

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

	return tcp_send_control(tsock, &tcp, sizeof(tcp));
}

/*
 *
 */
error_t tcp_send_rst(struct tcp_sock *tsock, u32 seq)
{
	struct tcp_header tcp;

	memset(&tcp, 0, sizeof(tcp));
	tcp.rst = true;
	tcp.seq_num = htonl(seq);

	return tcp_send_control(tsock, &tcp, sizeof(tcp));
}

/*
 *
 */
error_t tcp_send_syn(struct tcp_sock *tsock, bool ack)
{
	struct {
		struct tcp_header hdr;
		u8 options[4];
	} tcp;
	struct tcp_tlv_option *opt;

	memset(&tcp, 0, sizeof(tcp));
	tcp.hdr.syn = true;
	tcp.hdr.ack = ack;
	tcp.hdr.ack_num = htonl(tsock->tcb.recv.next);

	opt = (void *)&tcp.options[0];
	opt->type = TCP_OPT_MSS;
	opt->length = sizeof(*opt) + sizeof(u16);
	*(u16 *)opt->value = htons(TCP_IPV4_MSS);

	return tcp_send_control(tsock, &tcp.hdr, sizeof(tcp));
}

/*
 * Send as much segment as possible from previously buffered data.
 */
error_t tcp_send_pending(struct tcp_sock *tsock)
{
	struct tcb *tcb = &tsock->tcb;
	struct net_route *route = &tsock->isock.route;
	struct packet *packet;
	size_t pending;
	size_t usable;
	size_t payload_size;
	bool needs_fin;
	unsigned int mss;
	struct {
		struct tcp_header hdr;
	} tcp;

	tcp.hdr.psh = true;
	tcp.hdr.ack = true;
	tcp.hdr.ack_num = tcb->recv.next;
	tcp.hdr.data_offset = sizeof(tcp) / sizeof(u32);

	mss = tcp_effective_mss(tsock, &tcp.hdr);

	while (true) {
		needs_fin = !tsock->tcb.fin_sent && (tsock->fsm.cur_state == TCP_LAST_ACK ||
						     tsock->fsm.cur_state == TCP_FIN_WAIT_1);

		/*
		 * Avoid sending tinygram when the peer moves its window's righe edge
		 * in small increments (Silly Window Syndrom).
		 *
		 * NOTE: We forcefully set the PSH flag for all packets transmitted via send().
		 *
		 * @see 3.8.6.2.1 - SWS: Sender's Algorithm
		 */
		usable = tcb->send.unack + tcb->send.window_size - tcb->send.next;
		pending = ringbuffer_remaining(&tsock->pending);

		payload_size = MIN(pending, usable);
		if (payload_size >= mss) {
			payload_size = mss;
		} else if (tcb->send.next == tcb->send.unack) {
			if (pending <= usable) {
				payload_size = pending;
				tcp.hdr.psh = true;
				if (needs_fin && pending + 1 <= usable)
					tcp.hdr.fin = true;
			} else if (payload_size >= (tcb->send.max_window_size / 2)) {
				/* payload_size = payload_size */
			} else
				break;
		} else
			break;

		if (payload_size == 0 && !tcp.hdr.fin)
			break;

		/*
		 * Build and send a segment with the content of the pending buffer.
		 */
		packet = ipv4_build_packet(route, IPPROTO_TCP, &tcp.hdr, tcp_header_size(&tcp.hdr),
					   NULL, payload_size);
		if (IS_ERR(packet))
			return ERR_FROM_PTR(packet);

		ringbuffer_peek(&tsock->pending, packet_payload(packet), payload_size);
		if (!tcp_transmit_segment(tsock, route, packet)) {
			packet_free(packet);
			return E_NO_BUFFER_SPACE;
		}

		/* Packet sent, remove payload from the pending buffer. */
		ringbuffer_drop(&tsock->pending, payload_size);
	}

	return E_SUCCESS;
}

/*
 * Send FIN segment.
 */
error_t tcp_send_fin(struct tcp_sock *tsock)
{
	/* A FIN is automatically appended to the last buffered segment
	 * when in the appropriate state (i.e. FIN-WAIT-1, LAST-ACK). */
	return tcp_send_pending(tsock);
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
	if (seg->syn && (ipv4_is_broadcast(ip->daddr) || ipv4_is_multicast(ip->daddr))) {
		log_info("broadcast/mcast");
		goto discard;
	}

	/* Discard segments containing an invalid checksum. */
	if (tcp_checksum(ip->daddr, ip->saddr, seg, packet_payload(packet),
			 packet_payload_size(packet))) {
		log_info("invalid checksum");
		goto discard;
	}

	spinlock_acquire(&tcp_sockets_lock);

	/*
	 * Find the local socket associated with this connection.
	 */
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

	/* We must acquire a reference in case the socket transitions to the CLOSED state
	 * due to the reception of this segment. Do it while holding tcp_sockets_lock
	 * to stay in sync with tcp_enter_closed().
	 *
	 * NOTE: The hashtable could well contain a CLOSED socket since there's a small delay
	 *       between the moment the socket's state is update by the API and the moment
	 *       tcp_enter_closed() calls removes the hashtable entry.
	 */
	if (entry) {
		tsock = container_of(entry, struct tcp_sock, hash);
		if (tsock->fsm.cur_state == TCP_CLOSED)
			entry = NULL;
		else
			socket_get(tsock->socket);
	}

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
	ev.tsock = tsock;
	ev.seg = seg;
	ev.seg_len = tcp_segment_len(seg, packet_payload_size(packet));
	ev.seg_packet = packet;
	socket_lock(tsock->socket);
	fsm_run(&tsock->fsm, &ev);
	socket_unlock(tsock->socket);
	socket_put(tsock->socket);

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
	ev.type = TCP_EV_TIMEOUT;

	fsm_run(&tsock->fsm, &ev);
}

/*
 *
 */
static error_t af_inet_tcp_send_one(struct socket *socket, const struct iovec *iov)
{
	struct tcp_sock *tsock = socket->data;

	if (ringbuffer_remaining(&tsock->pending) < iov->iov_len) {
		/* Already checked by af_inet_tcp_sendmsg(), should never happen. */
		WARN("no space in ringbuffer while sending, "
		     "was it modified without holding the lock ?");
		return E_NO_BUFFER_SPACE;
	}

	ringbuffer_push(&tsock->pending, iov->iov_base, iov->iov_len);

	return E_SUCCESS;
}

/*
 * Send TCP segment to a peer.
 *
 * @see 3.10.2 - SEND Call
 */
static ssize_t af_inet_tcp_sendmsg(struct socket *socket, const struct msghdr *msg, int flags)
{
	struct tcp_sock *tsock;
	enum tcp_state state;
	size_t total_size;
	error_t err;

	total_size = 0;
	for (size_t i = 0; i < msg->msg_iovlen; ++i)
		total_size += msg->msg_iov[i].iov_len;

	socket_lock(socket);
	tsock = socket->data;
	state = tsock->fsm.cur_state;

	switch (state) {
	case TCP_LISTEN:
		err = E_NOT_SUPPORTED;
		goto fail;

	case TCP_SYN_SENT:
	case TCP_SYN_RECEIVED:
	case TCP_ESTABLISHED:
	case TCP_CLOSE_WAIT:
		break;

	case TCP_FIN_WAIT_1:
	case TCP_FIN_WAIT_2:
	case TCP_CLOSING:
	case TCP_LAST_ACK:
	case TCP_TIME_WAIT:
		err = E_CONNEXION_RESET;
		goto fail;
	default:
		assert_not_reached();
	}

	if (ringbuffer_remaining(&tsock->pending) < total_size) {
		/* TODO: Blocking send */
		err = E_WOULD_BLOCK;
		goto fail;
	}

	for (size_t i = 0; i < msg->msg_iovlen; ++i) {
		err =  af_inet_tcp_send_one(socket, &msg->msg_iov[i]);
		if (err)
			goto fail;
	}

	switch (state) {
	case TCP_SYN_SENT:
	case TCP_SYN_RECEIVED:
		/* Data is queued for when entering the ESTABLISHED state. */
		break;
	default:
		tcp_send_pending(tsock);
		break;
	}

	socket_unlock(socket);
	return total_size;
fail:
	socket_unlock(socket);
	return -err;
}

/*
 *
 */
static ssize_t af_inet_tcp_recv_one(struct socket *socket, struct iovec *iov, bool nonblock)
{
	struct tcp_sock *tsock = socket->data;
	struct packet *packet = NULL;
	ssize_t total_size = 0;
	size_t to_read = iov->iov_len;
	node_t *node;

retry:
	spinlock_acquire(&socket->rx_lock);
	while (!queue_is_empty(&socket->rx_packets)) {
		size_t size;

		node = queue_peek(&socket->rx_packets);
		packet = container_of(node, struct packet, rx_this);
		if (to_read >= packet_read_size(packet))
			queue_dequeue(&socket->rx_packets);

		size = packet_pop(packet, iov->iov_base, to_read);
		tcp_window_user_receive(&tsock->tcb, size);
		total_size += size;
		to_read -= size;

		if (to_read == 0)
			goto out;
		if (packet->l4.tcp->psh)
			goto out;
	}

	/* Not enough segments to fill the vector, wait for more ... */
	if (!nonblock) {
		spinlock_acquire(&socket->rx_blocked.lock);
		spinlock_release(&socket->rx_lock);
		waitqueue_enqueue_locked(&socket->rx_blocked, current);
		goto retry;
	} else
		packet = PTR_ERR(E_WOULD_BLOCK);

out:
	spinlock_release(&socket->rx_lock);
	return total_size;
}

/*
 * Receive TCP segment from a peer.
 *
 * @see 3.10.3 - RECEIVE Call
 */
static ssize_t af_inet_tcp_recvmsg(struct socket *socket, struct msghdr *msg, int flags)
{
	struct tcp_sock *tsock;
	struct packet *packet;
	size_t total_size;
	error_t err;

	if (flags & ~O_NONBLOCK) {
		not_implemented("recvmsg: flags (%x)", flags & ~O_NONBLOCK);
		return -E_NOT_SUPPORTED;
	}

	total_size = 0;
	for (size_t i = 0; i < msg->msg_iovlen; ++i)
		total_size += msg->msg_iov[i].iov_len;

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
		break;

	case TCP_CLOSE_WAIT:
		/* Since the peer has already sent FIN, RECEIVEs
		 * must be satisfied by data already on hand. */
		if (queue_is_empty(&socket->rx_packets)) {
			flags |= O_NONBLOCK;
			break;
		}

		fallthrough;

	case TCP_CLOSING:
	case TCP_LAST_ACK:
	case TCP_TIME_WAIT:
		/* return value of 0 means 'connection is closing' */
		socket_unlock(socket);
		return 0;

	default:
		assert_not_reached();
	}

	/* We don't access tsock's internal state anymore, only the socket's RX queue. */
	socket_unlock(socket);

	/*
	 * Check for EWOULDBLOCK before popping the segments to avoid data loss.
	 */
	spinlock_acquire(&socket->rx_lock);
	FOREACH_LLIST_ENTRY(packet, &socket->rx_packets, rx_this) {
		total_size -= MIN(total_size, packet_read_size(packet));
		if (packet->l4.tcp->psh) {
			total_size = 0;
			break;
		}
	}
	spinlock_release(&socket->rx_lock);
	if (total_size > 0 && flags & O_NONBLOCK)
		return -E_WOULD_BLOCK;

	/*
	 * Finally, fill the io vectors with the segment's content.
	 */
	total_size = 0;
	for (size_t i = 0; i < msg->msg_iovlen; ++i) {
		struct iovec *iov = &msg->msg_iov[i];
		ssize_t size;

		size = af_inet_tcp_recv_one(socket, iov, flags & O_NONBLOCK);
		ASSERT(size > 0); /* total size should have been checked beforehand */

		total_size += size;
		if ((size_t)size < iov->iov_len)
			break;
	}

	return total_size;
fail:
	socket_unlock(socket);
	return -err;
}

/*
 * Bind TCP socket to a local address.
 */
static error_t af_inet_tcp_bind(struct socket *socket, const struct sockaddr *addr, socklen_t len)
{
	struct tcp_sock *tsock = socket->data;
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
	spinlock_acquire(&tcp_sockets_lock);
	for (; port_start <= port_end; ++port_start) {
		key.addr = sin.sin_addr.s_addr;
		key.port = htons(port_start);
		if (hashtable_find(&tcp_sockets, &key))
			continue;

		sin.sin_port = key.port;
		err = inet_sock_bind(&tsock->isock, &sin);
		if (err) /* invalid local address */
			break;

		hashtable_insert(&tcp_sockets, &tsock->hash);
		socket->state |= SOCKET_BOUND;
		log_dbg("socket bound to %p4 port %d",
			 &tsock->isock.addr,
			 htons(tsock->isock.port));
		break;
	}
	spinlock_release(&tcp_sockets_lock);

	return err;
}

/*
 * Open active TCP connection
 */
static error_t af_inet_tcp_connect(struct socket *socket, const struct sockaddr *addr, socklen_t len)
{
	struct tcp_sock *tsock = socket->data;
	enum tcp_state state = tsock->fsm.cur_state;

	if (state != TCP_CLOSED)
		return E_INVAL;

	if (!socket_is_bound(socket)) {
		struct sockaddr_in sin = {
			.sin_family = AF_INET,
			.sin_addr.s_addr = INADDR_ANY,
			.sin_port = 0,
		};
		error_t err;

		err = af_inet_tcp_bind(socket, (void *)&sin, sizeof(sin));
		if (err)
			return err;
	}

	tcp_set_state(tsock, TCP_SYN_SENT);

	return E_SUCCESS;
}

/*
 * Open passive TCP connection
 */
static error_t af_inet_tcp_listen(struct socket *socket)
{
	struct tcp_sock *tsock = socket->data;
	enum tcp_state state = tsock->fsm.cur_state;

	if (state != TCP_CLOSED)
		return E_INVAL;

	if (!socket_is_bound(socket)) {
		struct sockaddr_in sin = {
			.sin_family = AF_INET,
			.sin_addr.s_addr = INADDR_ANY,
			.sin_port = 0,
		};
		error_t err;

		err = af_inet_tcp_bind(socket, (void *)&sin, sizeof(sin));
		if (err)
			return err;
	}

	tcp_set_state(tsock, TCP_LISTEN);

	return E_SUCCESS;
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
	struct tcp_sock *tsock = socket->data;
	enum tcp_state state = tsock->fsm.cur_state;

	switch (state) {
	case TCP_LISTEN:
	case TCP_SYN_SENT:
		break;

	case TCP_SYN_RECEIVED:
		not_implemented("%s: close()", tcp_state_name(state));
		goto fail;

	case TCP_ESTABLISHED:
		tcp_set_state(tsock, TCP_FIN_WAIT_1);
		break;

	case TCP_CLOSE_WAIT:
		tcp_set_state(tsock, TCP_LAST_ACK);
		break;

	case TCP_FIN_WAIT_1:
	case TCP_FIN_WAIT_2:
	case TCP_CLOSING:
	case TCP_LAST_ACK:
	case TCP_TIME_WAIT:
		goto fail;

	default:
		assert_not_reached();
	}

fail:
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
	if (tsock->pending.buf_start)
		kfree(tsock->pending.buf_start);
	kfree(tsock);
}

/*
 *
 */
static error_t af_inet_tcp_init(struct socket *socket)
{
	struct tcp_sock *tsock;
	void *buffer;

	tsock = kcalloc(1, sizeof(*tsock), KMALLOC_KERNEL);
	if (!tsock)
		return E_NOMEM;
	socket->data = tsock;

	buffer = kmalloc(TCP_DEFAULT_BUFFER_SIZE, KMALLOC_KERNEL);
	if (!buffer)
		return E_NOMEM;
	ringbuffer_init(&tsock->pending, buffer, TCP_DEFAULT_BUFFER_SIZE);

	tsock->time_wait_timeout = timeout_new(tcp_timeout, tsock);
	if (!tsock->time_wait_timeout)
		return E_NOMEM;

	inet_sock_init(&tsock->isock);
	fsm_init(&tsock->fsm, tcp_fsm_states, TCP_STATE_COUNT, TCP_CLOSED);
	INIT_HASHTABLE_ENTRY(tsock->hash, tsock);
	INIT_QUEUE(tsock->xmit_queue);

	tsock->socket = socket_get(socket); /* reference released when entering CLOSED state. */

	return E_SUCCESS;
}

struct socket_protocol_ops af_inet_tcp_ops = {
	.init = af_inet_tcp_init,
	.bind = af_inet_tcp_bind,
	.listen = af_inet_tcp_listen,
	.connect = af_inet_tcp_connect,
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
