/*
 * TCP protocol state machine implementation.
 *
 * ## References
 *
 * - RFC 9293 - Transmission Control Protocol (TCP)
 *   * 3.10 - Event processing
 */

#define LOG_DOMAIN "tcp"

#include <kernel/kmalloc.h>
#include <kernel/net/tcp.h>
#include <kernel/net/packet.h>
#include <kernel/timer.h>

#include <netinet/in.h>

static inline struct tcp_sock *fsm_to_tsock(struct fsm *fsm)
{
	return container_of(fsm, struct tcp_sock, fsm);
}

static inline bool is_active_open(const struct tcp_sock *tsock)
{
	return tsock->tcb.initial_state == TCP_SYN_SENT;
}

static inline bool is_passive_open(const struct tcp_sock *tsock)
{
	return tsock->tcb.initial_state == TCP_SYN_RECEIVED;
}

static inline bool fin_acknowledged(const struct tcp_sock *tsock)
{
	return tsock->tcb.fin_sent && tsock->tcb.send.unack > tsock->tcb.fin_seq_num;
}

const char *tcp_state_name(enum tcp_state state)
{
	static const char *names[] = {
		"CLOSED",
		"LISTEN",
		"SYN-SENT",
		"SYN-RECEIVED",
		"ESTABLISHED",
		"FIN-WAIT-1",
		"FIN-WAIT-2",
		"CLOSE-WAIT",
		"CLOSING",
		"LAST-ACK",
		"TIME-WAIT",
	};

	if (state >= ARRAY_SIZE(names))
		return "INVALID";
	return names[state];
}

/*
 * @see 3.7.1 Maximum Segment Size Option
 */
static void tcp_handle_mss_option(struct tcp_sock *tsock, struct tcp_header *seg)
{
	struct tcp_tlv_option *opt;

	FOREACH_TCP_OPTION(opt, seg) {
		switch (opt->type) {
			case TCP_OPT_MSS:
				tcp_set_send_mss(tsock, ntohs(*(u16 *)opt->value));
				return;
		}
	}

	/* Fallback to default value if not specified. */
	tcp_set_send_mss(tsock, TCP_IPV4_MSS);
}

/*
 * Enter CLOSED state.
 */
static void tcp_enter_closed(struct fsm *fsm, unsigned int state)
{
	struct tcp_sock *tsock = fsm_to_tsock(fsm);

	locked_scope(&tcp_sockets_lock)
		hashtable_remove_entry(&tsock->hash);

	/* TODO: Cancel blocked recv()/send() */
	tcp_transmit_queue_flush(tsock);

	tsock->socket->state = 0;
	socket_put(tsock->socket);
}

/*
 * Exit CLOSED state.
 */
static void tcp_exit_closed(struct fsm *fsm, unsigned int state)
{
	struct tcp_sock *tsock = fsm_to_tsock(fsm);
	struct tcb *tcb = &tsock->tcb;

	/*
	 * Initialize TCB.
	 */
	tcb->initial_state = state;
	tcb->iss = tcp_compute_isn();
	tcb->recv.buff = TCP_DEFAULT_BUFFER_SIZE;
	tcb->recv.window_size = TCP_DEFAULT_BUFFER_SIZE;
	tcb->send.unack = tcb->iss;
	tcb->send.next = tcb->iss;
}

/*
 * Enter LISTEN state.
 */
static void tcp_enter_listen(struct fsm *fsm, unsigned int state)
{
	struct tcp_sock *tsock = fsm_to_tsock(fsm);

	tsock->socket->state |= SOCKET_LISTEN;
}

/*
 * Exit LISTEN state.
 */
static void tcp_exit_listen(struct fsm *fsm, unsigned int next_state)
{
	/* Cannot leave the LISTEN state once entered. */
	ASSERT(next_state == TCP_CLOSED);
}

/*
 * Enter the SYN-SENT state.
 */
static void tcp_enter_syn_sent(struct fsm *fsm, unsigned int prev_state)
{
	struct tcp_sock *tsock = fsm_to_tsock(fsm);
	struct tcb *tcb = &tsock->tcb;

	tsock->socket->state |= SOCKET_CONNECTING;
	locked_scope(&tcp_sockets_lock)
		hashtable_insert(&tcp_sockets, &tsock->hash);

	/*
	 * Configure a default send window size large enough for a singlge SYN.
	 *
	 * In a normal situation the receiver is in the LISTEN state, which does
	 * not have a properly configured window, and does not check the sequence
	 * number's validity. In any other case
	 */
	tcp_window_update_send(tcb, 1, tcb->recv.next, tcb->send.unack);

	tcp_send_syn(tsock, false);
}

/*
 * Enter the SYN-RECEIVED state.
 *
 * This state is entered after receiving a SYN packet during the handshake.
 * When in this state we must ACK the received SYN packet, and send one
 * of our own (SYN+ACK).
 */
static void tcp_enter_syn_received(struct fsm *fsm, unsigned int prev_state)
{
	struct tcp_sock *tsock = fsm_to_tsock(fsm);

	if (!(tsock->socket->state & SOCKET_CONNECTING)) {
		/* Already present inside the hashtable if coming from SYN-SENT. */
		tsock->socket->state |= SOCKET_CONNECTING;
		locked_scope(&tcp_sockets_lock)
			hashtable_insert(&tcp_sockets, &tsock->hash);
	}

	tcp_send_syn(tsock, true);
}

/*
 * Enter the SYN-RECEIVED state.
 *
 * Add this connection to the listening socket's backlog (only for passive ones).
 */
static void tcp_exit_syn_received(struct fsm *fsm, unsigned int next_state)
{
	struct tcp_sock *tsock = fsm_to_tsock(fsm);
	struct socket_backlog_entry backlog_entry;
	struct socket *listen_socket = tsock->listen_socket;

	if (!is_passive_open(tsock))
		return;

	socket_lock(listen_socket);

	if (next_state != TCP_ESTABLISHED) {
		/* Connection cancelled. */
		socket_backlog_release(listen_socket);
		socket_put(tsock->socket);
	} else {
		/* Reference already acquired in tcp_listen(), see related comment. */
		backlog_entry.socket = tsock->socket;
		backlog_entry.salen = sizeof(tsock->isock.route.dst.ip);
		memcpy(&backlog_entry.addr, &tsock->isock.route.dst.ip, backlog_entry.salen);
		socket_backlog_push(tsock->listen_socket, &backlog_entry);
	}

	socket_unlock(listen_socket);
	socket_put(listen_socket);
	tsock->listen_socket = NULL;
}

/*
 * Enter the ESTABLISHED state.
 */
static void tcp_enter_established(struct fsm *fsm, unsigned int prev_state)
{
	struct tcp_sock *tsock = fsm_to_tsock(fsm);

	tsock->socket->state |= SOCKET_CONNECTED;
	tsock->socket->state &= ~SOCKET_CONNECTING;

	/* Transmit segments enqueued by a SEND in the SYN-SENT and SYN-RECEIVED states. */
	tcp_send_pending(tsock);

}

/*
 * Enter the FIN-WAIT-1 state.
 */
static void tcp_enter_fin_wait_1(struct fsm *fsm, unsigned int prev_state)
{
	struct tcp_sock *tsock = fsm_to_tsock(fsm);

	tcp_send_fin(tsock);
}

/*
 * Enter the LAST_ACK state.
 */
static void tcp_enter_last_ack(struct fsm *fsm, unsigned int prev_state)
{
	struct tcp_sock *tsock = fsm_to_tsock(fsm);

	tcp_send_fin(tsock);
}

/*
 * Enter the TIME-WAIT state, start the expiration timer.
 */
static void tcp_enter_timewait(struct fsm *fsm, unsigned int prev_state)
{
	struct tcp_sock *tsock = fsm_to_tsock(fsm);

	timeout_arm(tsock->time_wait_timeout, MS(2 * TCP_MSL));
}

/*
 * Exit the TIME-WAIT state.
 */
static void tcp_exit_timewait(struct fsm *fsm, unsigned int next_state)
{
	struct tcp_sock *tsock = fsm_to_tsock(fsm);

	timeout_cancel(tsock->time_wait_timeout);
}

/*
 * Handle events in CLOSED state.
 *
 * CLOSED is a 'virtual' state representing an non-existing/unreachable connection.
 * This case should have already been handled by the upper layer, and this function
 * should never be executed
 */
static void tcp_closed(struct fsm *fsm, void *data)
{
	assert_not_reached();
}

/*
 * Handle events in LISTEN state.
 *
 * @see 3.10.7.2 - SEGMENT ARRIVES (LISTEN)
 */
static void tcp_listen(struct fsm *fsm, void *data)
{
	struct tcp_sock *tsock = fsm_to_tsock(fsm);
	struct socket *socket = tsock->socket;
	struct tcp_fsm_ev *ev = data;
	struct tcp_header *seg = ev->seg;
	struct socket *conn = NULL;
	struct tcp_sock *conn_tsock;
	u32 seq = htonl(seg->seq_num);
	struct sockaddr_in peer_addr = {
	    .sin_family = AF_INET,
	    .sin_addr.s_addr = ev->seg_packet->l3.ipv4->saddr,
	    .sin_port = ev->seg_packet->l4.tcp->sport,
	};

	/* There shouldn't be any active timeout in this state. */
	if (WARN_ON(ev->type != TCP_EV_SEGMENT_ARRIVES))
		return;

	if (seg->rst)
		goto discard;

	if (seg->ack) {
		/* FIXME: cannot use tcp_send_rst() on a non-connected socket */
		not_implemented("%s: ACK", tcp_state_name(fsm->cur_state));
		goto discard;
	}

	if (!seg->syn)
		goto discard;

	/*
	 * FIXME: Duplicated socket on half open connection retransmit !!!!!!!!!!!!.
	 */

	/* Backlog is full, prevent new connections for now and rely on retransmissions. */
	if (socket_backlog_reserve(socket))
		goto discard;

	conn = socket_alloc();
	if (!conn)
		goto backlog_discard;

	if (socket_init(conn, AF_INET, SOCK_STREAM, IPPROTO_TCP))
		goto backlog_discard;

	socket_lock(conn);

	conn_tsock = conn->data;
	conn_tsock->listen_socket = socket_get(tsock->socket);
	conn_tsock->isock.addr = ev->seg_packet->l3.ipv4->daddr;
	conn_tsock->isock.port = tsock->isock.port;
	if (inet_sock_connect(&conn_tsock->isock, &peer_addr)) {
		WARN("failed to find incoming connection's route?");
		socket_unlock(conn);
		goto backlog_discard;
	}

	/*
	 * Handle SYN segment's content.
	 */
	conn_tsock->tcb.irs = seq;
	conn_tsock->tcb.recv.next = seq + 1;
	tcp_handle_mss_option(conn_tsock, seg);

	tcp_set_state(conn_tsock, TCP_SYN_RECEIVED);
	tcp_window_update_send(&conn_tsock->tcb, htonl(seg->window), seq, 0);

	socket_unlock(conn);

	/*
	 * NOTE: We are leaving this function with an extra reference to the conn
	 *       socket (the one from socket_init()). This reference is already owned
	 *       by the listening socket's backlog, even though no entry has been addded
	 *       yet. No additional reference must be obtained when doing so, but this
	 *       extra one must be released in case the connection is aborted.
	 */
	return;

backlog_discard:
	socket_backlog_release(socket);
discard:
	if (conn)
		socket_put(conn);
}

/*
 * The sixth, seventh and eight steps of 3.10.7.4.
 *
 * These steps are re-used to handle segments with text in the SYN-SENT state
 * when transitioning to ESTABLISHED after receiving an ACK containing data.
 *
 * @see 3.10.7.4 - SEGMENT ARRIVES (Other States)
 */
static void tcp_handle_segment_text(struct fsm *fsm, struct tcp_fsm_ev *ev)
{
	struct tcp_sock *tsock = fsm_to_tsock(fsm);
	enum tcp_state state = fsm->cur_state;
	struct tcp_header *seg = ev->seg;
	struct packet *seg_packet = ev->seg_packet;
	u32 seq = ntohl(seg->seq_num);

	/*
	 * Sixth, check the URG bit (TODO).
	 */
	if (seg->urg) {
		switch (state) {
		case TCP_ESTABLISHED:
		case TCP_FIN_WAIT_1:
		case TCP_FIN_WAIT_2:
			/* TODO */
			not_implemented("%s: URG", tcp_state_name(state));
			break;

		default:
			/* ignore */
			break;
		}
	}

	/*
	 * Seventh, process the segment text.
	 */
	switch (state) {
	case TCP_ESTABLISHED:
	case TCP_FIN_WAIT_1:
	case TCP_FIN_WAIT_2:
		/* Skip IP+TCP header when reading the packet. */
		packet_pop(seg_packet, NULL, packet_header_size(seg_packet));
		if (socket_enqueue_packet(tsock->socket, ev->seg_packet))
			goto discard;
		ASSERT(tcp_window_receive(&tsock->tcb, seq, ev->seg_len));

		/* Try and piggyback the ACK on a segment containing data. */
		if (tcp_send_pending(tsock) != E_SUCCESS)
			tcp_send_ack(tsock);
		break;

	default:
		/* ignore */
		break;
	}

	/*
	 * Eight, check the FIN bit.
	 *
	 * TODO: Interrupt recv() with ECONNRESET
	 */
	if (seg->fin) {
		switch (state) {
		case TCP_SYN_RECEIVED:
		case TCP_ESTABLISHED:
			tcp_set_state(tsock, TCP_CLOSE_WAIT);
			break;

		case TCP_FIN_WAIT_1:
			tcp_set_state(tsock, TCP_CLOSING);
			break;

		case TCP_FIN_WAIT_2:
			tcp_set_state(tsock, TCP_TIME_WAIT);
			break;

		case TCP_CLOSE_WAIT:
		case TCP_CLOSING:
		case TCP_LAST_ACK:
			/* Remain in the current state. */
			break;

		case TCP_TIME_WAIT:
			/* Received a retransmitted FIN. */
			tcp_send_ack(tsock);
			timeout_arm(tsock->time_wait_timeout, MS(2 * TCP_MSL));
			break;

		default:
			/* ignore */
			break;
		}
	}

	return;

discard:
	packet_free(ev->seg_packet);
	return;
}

/*
 * Handle events in SYN-SENT state.
 *
 * @see 3.10.7.3 - SEGMENT ARRIVES (SYN-SENT)
 */
static void tcp_syn_sent(struct fsm *fsm, void *data)
{
	struct tcp_sock *tsock = fsm_to_tsock(fsm);
	struct tcp_fsm_ev *ev = data;
	struct tcp_header *seg = ev->seg;
	struct tcb *tcb = &tsock->tcb;
	u32 ack = ntohl(seg->ack_num);
	u32 seq = ntohl(seg->seq_num);

	/* There shouldn't be any active timeout in this state. */
	if (WARN_ON(ev->type != TCP_EV_SEGMENT_ARRIVES))
		return;

	if (seg->ack) {
		if (!tcp_window_accept_ack(tcb, ack)) {
			if (!seg->rst)
				tcp_send_rst(tsock, ack);
			goto discard;
		}
	}

	if (seg->rst) {
		/* TODO: set ECONNRESET */
		tcp_set_state(tsock, TCP_CLOSED);
		goto discard;
	}

	if (seg->syn) {
		tcb->irs = seq;
		tcb->recv.next = seq + 1;
		tcp_handle_mss_option(tsock, seg);

		if (seg->ack) {
			tcp_transmit_queue_ack(tsock, ack);
			tcp_window_ack(tcb, ack);
		}

		if (tcb->send.unack > tcb->iss) {
			/* Our SYN was ACKed */
			tcp_set_state(tsock, TCP_ESTABLISHED);
			tcp_send_ack(tsock);

			/* This ACK is allowed to contain data. */
			return tcp_handle_segment_text(fsm, ev);
		} else {
			/* Simultaneous open. */
			tcp_set_state(tsock, TCP_SYN_RECEIVED);
			tcp_window_update_send(tcb, ntohl(seg->window), seq, ack);
		}
	}

	/* Packet holds no data, discard it. */

discard:
	packet_free(ev->seg_packet);
	return;
}

/*
 * Process segment in all states except CLOSED, LISTEN and SYN-SENT.
 *
 * @see 3.10.7.4 - SEGMENT ARRIVES (Other States)
 */
static void tcp_others_handle_segment(struct fsm *fsm, void *data)
{
	enum tcp_state state = fsm->cur_state;
	struct tcp_sock *tsock = fsm_to_tsock(fsm);
	struct tcp_fsm_ev *ev = data;
	struct tcb *tcb = &tsock->tcb;
	struct tcp_header *seg = ev->seg;
	struct packet *packet = ev->seg_packet;
	size_t payload_skip;
	u32 seq = ntohl(seg->seq_num);
	u32 ack = ntohl(seg->ack_num);

	/*
	 * First, check sequence number.
	 */
	if (!tcp_window_accept(tcb, seq, ev->seg_len)) {
		if (!seg->rst)
			tcp_send_ack(tsock);
		goto discard;
	}
	tcp_window_trim_segment(tcb, seg, &ev->seg_len, &payload_skip);
	if (payload_skip) {
		ASSERT(payload_skip <= packet_payload_size(packet));
		memmove(packet_payload(packet),
			packet_payload(packet) + payload_skip,
			packet_payload_size(packet) - payload_skip);
		packet_pull(packet, payload_skip);
	}

	seq = ntohl(seg->seq_num); /* may have been update when trimming */
	if (seq != tcb->recv.next) {
		/* Segments arrived out of order.
		 *
		 * TODO: Out-of-order segment should be held for later processing.
		 */
		goto discard;
	}

	/*
	 * Second, check the RST bit.
	 */
	if (seg->rst) {
		/* TODO:
		 *	if (is_active_open(tsock))
		 *		connect returns ECONNREFUSED;
		 *	else
		 *		outstanding sends/recvs() return ECONNRESET;
		 */
		tcp_set_state(tsock, TCP_CLOSED);
		goto discard;
	}

	/* Third, check security (ignored). */

	/*
	 * Fourth, check the SYN bit.
	 *
	 * We follow the behaviour defined in RFC793 When receiving a second SYN
	 * after the first one has been acknowledged already: reset the connexion.
	 *
	 * NOTE: The case of a retransmitted SYN is taken care of when trimming
	 *       acknowledged bytes from the segment in the first step.
	 */
	if (seg->syn) {
		if (state == TCP_SYN_RECEIVED && is_passive_open(tsock)) {
			/* Abort handshake */
			tcp_set_state(tsock, TCP_CLOSED);
			goto discard;
		}

		/* TODO: ECONNRESET */
		tcp_send_rst(tsock, seq);
		tcp_set_state(tsock, TCP_CLOSED);
		goto discard;
	}

	/*
	 * Fifth, check the ACK bit.
	 */
	if (!seg->ack)
		goto discard;

	switch (state) {
	case TCP_SYN_RECEIVED:
		if (!tcp_window_accept_ack(tcb, ack)) {
			tcp_send_rst(tsock, ack);
			break;
		}

		/* Our SYN+ACK was ACKed. */
		tcp_window_update_send(tcb, ntohl(seg->window), seq, ack);
		tcp_set_state(tsock, TCP_ESTABLISHED);
		fallthrough;

	case TCP_ESTABLISHED:
	case TCP_FIN_WAIT_1:
	case TCP_FIN_WAIT_2:
	case TCP_CLOSE_WAIT:
	case TCP_CLOSING:
		if (tcp_window_accept_ack(tcb, ack)) {
			tcp_transmit_queue_ack(tsock, ack);
			tcp_window_ack(tcb, ack);
		} else if (!tcp_window_ack_is_duplicate(tcb, ack)) {
			/* ACK for data that was never sent */
			tcp_send_ack(tsock);
			goto discard;
		}
		tcp_window_update_send(tcb, ntohl(seg->window), seq, ack);

		/* The following states require additional processing */
		switch (state) {
		case TCP_FIN_WAIT_1:
			if (fin_acknowledged(tsock))
				tcp_set_state(tsock, TCP_FIN_WAIT_2);
			break;
		case TCP_CLOSING:
			if (fin_acknowledged(tsock))
				tcp_set_state(tsock, TCP_TIME_WAIT);
			break;
		default:
			break;
		}
		break;

	case TCP_LAST_ACK:
		if (fin_acknowledged(tsock))
			tcp_set_state(tsock, TCP_CLOSED);
		break;

	case TCP_TIME_WAIT:
		break;

	default:
		assert_not_reached();
	}

	return tcp_handle_segment_text(fsm, ev);

discard:
	packet_free(ev->seg_packet);
	return;
}

/*
 *
 */
static void tcp_others(struct fsm *fsm, void *data)
{
	struct tcp_sock *tsock = fsm_to_tsock(fsm);
	struct tcp_fsm_ev *ev = data;

	switch (ev->type) {
	case TCP_EV_SEGMENT_ARRIVES:
		return tcp_others_handle_segment(fsm, data);
	case TCP_EV_TIMEOUT:
		tcp_set_state(tsock, TCP_CLOSED);
		break;
	}
}

/*
 * Update a TCP connection's state.
 */
void tcp_set_state(struct tcp_sock *tsock, unsigned int state)
{
	if (state != tsock->fsm.cur_state) {
		log_dbg("[%p4:%d] %s -> %s", &tsock->isock.addr, ntohs(tsock->isock.port),
			tcp_state_name(tsock->fsm.cur_state), tcp_state_name(state));
		fsm_set_state(&tsock->fsm, state);
	}
}

// clang-format off

const struct fsm_state tcp_fsm_states[] = {
	[TCP_CLOSED] =		{ tcp_closed,   tcp_enter_closed,       tcp_exit_closed },
	[TCP_LISTEN] =		{ tcp_listen,   tcp_enter_listen,       tcp_exit_listen },
	[TCP_SYN_SENT] =	{ tcp_syn_sent, tcp_enter_syn_sent,     NULL },
	[TCP_SYN_RECEIVED] =	{ tcp_others,   tcp_enter_syn_received, tcp_exit_syn_received },
	[TCP_ESTABLISHED] =	{ tcp_others,   tcp_enter_established,  NULL },
	[TCP_FIN_WAIT_1] =	{ tcp_others,   tcp_enter_fin_wait_1,   NULL },
	[TCP_FIN_WAIT_2] =	{ tcp_others,   NULL,                   NULL },
	[TCP_CLOSE_WAIT] =	{ tcp_others,   NULL,                   NULL },
	[TCP_CLOSING] =		{ tcp_others,   NULL,                   NULL },
	[TCP_LAST_ACK] =	{ tcp_others,   tcp_enter_last_ack,     NULL },
	[TCP_TIME_WAIT] =	{ tcp_others,   tcp_enter_timewait,     tcp_exit_timewait },
};
