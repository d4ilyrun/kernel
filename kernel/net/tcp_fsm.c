/*
 * TCP protocol state machine implementation.
 *
 * ## TODO
 *
 * - 3.10.8 Timeouts
 *
 * ## References
 *
 * - RFC 9293 - Transmission Control Protocol (TCP)
 *   * 3.10 - Event processing
 */

#define LOG_DOMAIN "tcp"

#include <kernel/kmalloc.h>
#include <kernel/net/tcp.h>

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

static const char *tcp_state_name(unsigned int state)
{
	static const char *names[] = {
		"CLOSED",
		"LISTEN",
		"SYN-SENT",
		"SYN-RECEIVED",
		"ESTABLISHED",
		"FIN-WAIT_1",
		"FIN-WAIT_2",
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
 * Enter CLOSED state.
 *
 * TODO:
 * - Flush pending/retransmission queue
 */
static void tcp_enter_closed(struct fsm *fsm, unsigned int state)
{
	struct tcp_sock *tsock = fsm_to_tsock(fsm);

	tsock->socket->state = 0;		    /* reset state */
	memset(&tsock->tcb, 0, sizeof(struct tcb)); /* reset TCB */
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
	tcb->send.unack = tcb->iss;
	tcb->send.next = tcb->iss;
}

/*
 * Enter the SYN-SENT state.
 */
static void tcp_enter_syn_sent(struct fsm *fsm, unsigned int prev_state)
{
	struct tcp_sock *tsock = fsm_to_tsock(fsm);
	struct tcb *tcb = &tsock->tcb;

	/*
	 * Configure a default send window size large enough for a singlge SYN.
	 *
	 * In a normal situation the receiver is in the LISTEN state, which does
	 * not have a properly configured window, and does not check the sequence
	 * number's validity. In any other case
	 */
	tcp_window_update_send(tcb, 1, tcb->recv.next, tcb->send.unack);

	tsock->socket->state |= SOCKET_CONNECTING;
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

	tsock->socket->state |= SOCKET_CONNECTING;
	tcp_send_syn(tsock, true);
}

/*
 * Enter the ESTABLISHED state.
 */
static void tcp_enter_established(struct fsm *fsm, unsigned int prev_state)
{
	struct tcp_sock *tsock = fsm_to_tsock(fsm);

	tsock->socket->state |= SOCKET_CONNECTED;
	tsock->socket->state &= ~SOCKET_CONNECTING;

	/* Transmit all segments enqueued by a SEND in the SYN-SENT
	 * and SYN-RECEIVED states. */
	tcp_send_pending_segments(tsock);
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
	struct tcp_fsm_ev *ev = data;
	struct tcp_header *seg = ev->seg;
	struct socket *conn = NULL;
	struct tcp_sock *conn_tsock;
	struct tcp_tlv_option *opt;
	u32 seq = htonl(seg->seq_num);

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

	conn = socket_alloc();
	if (!conn)
		goto discard;

	/* TODO: Add socket to LISTEN socket backlog. */
	/* TODO: Add socket to hashtable (locking ?). */

	if (socket_init(conn, AF_INET, SOCK_STREAM, IPPROTO_TCP))
		goto discard;

	socket_lock(conn);

	conn_tsock = conn->data;
	conn_tsock->isock.addr = tsock->isock.addr;
	conn_tsock->isock.port = tsock->isock.port;
	if (inet_sock_connect(&conn_tsock->isock, &tsock->isock.route.dst.ip)) {
		WARN("failed to find incoming connection's route?");
		socket_unlock(conn);
		goto discard;
	}

	/*
	 * Initialize TCB.
	 */
	conn_tsock->tcb.irs = seq;
	conn_tsock->tcb.recv.next = conn_tsock->tcb.irs + 1;
	conn_tsock->tcb.send.mss = TCP_IPV4_MSS;

	FOREACH_TCP_OPTION(opt, seg) {
		switch (opt->type) {
		case TCP_OPT_MSS:
			conn_tsock->tcb.send.mss = *(u32 *)opt->value;
			continue;
		}
	}

	tcp_set_state(conn_tsock, TCP_SYN_RECEIVED);
	tcp_window_update_send(&conn_tsock->tcb, htonl(seg->window), seq, 0);

	socket_unlock(conn);

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
static void tcp_handle_segment_text(struct fsm *fsm, struct tcp_header *seg, size_t seg_len,
				    size_t payload_skip)
{
	struct tcp_sock *tsock = fsm_to_tsock(fsm);
	enum tcp_state state = fsm->cur_state;
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
		ASSERT(tcp_window_receive(&tsock->tcb, seq, seg_len));
		/* TODO: Ack should be piggybacked on a segment containing data
		 *       if one is available in the pending queue. */
		tcp_send_ack(tsock);
		break;
	default:
		/* ignore */
		break;
	}

	/*
	 * Eight, check the FIN bit.
	 */
	if (seg->fin) {
		switch (state) {
		case TCP_SYN_RECEIVED:
		case TCP_ESTABLISHED:
			tcp_set_state(tsock, TCP_CLOSE_WAIT);
			break;

		case TCP_FIN_WAIT_1:
			not_implemented("%s: FIN", tcp_state_name(state));
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
			/* TODO: restart 2 MSL time-wait timeout. */
			break;

		default:
			/* ignore */
			break;
		}
	}
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
		return;
	}

	if (seg->syn) {
		tcb->irs = seq;
		tcb->recv.next = seq + 1;
		if (seg->ack)
			tcp_window_ack(tcb, ack);

		if (tcb->send.unack > tcb->iss) {
			/* Our SYN was ACKed */
			tcp_set_state(tsock, TCP_ESTABLISHED);
			tcp_send_ack(tsock);

			/* This ACK is allowed to contain data. */
			return tcp_handle_segment_text(fsm, seg, ev->seg_len, 0);
		} else {
			/* Simultaneous open. */
			tcp_set_state(tsock, TCP_SYN_RECEIVED);
			tcp_window_update_send(tcb, ntohl(seg->window), seq, ack);
		}
	}

discard:
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
		if (tcp_window_accept_ack(tcb, ack)) {
			/* Our SYN+ACK packet was ackowledged. */
			tcp_window_update_send(tcb, ntohl(seg->window), seq, ack);
			tcp_set_state(tsock, TCP_ESTABLISHED);
		} else
			tcp_send_rst(tsock, ack);
		break;

	case TCP_ESTABLISHED:
	case TCP_FIN_WAIT_1:
	case TCP_FIN_WAIT_2:
	case TCP_CLOSE_WAIT:
	case TCP_CLOSING:
		if (tcp_window_ack(tcb, ack)) {
			/* TODO: Remove acked packets from retransmission queue */
		} else if (!tcp_window_ack_is_duplicate(tcb, ack)) {
			/* ACK for data that was never sent */
			tcp_send_ack(tsock);
			goto discard;
		}
		tcp_window_update_send(tcb, ntohl(seg->window), seq, ack);

		/* The following states require additional processing */
		switch (state) {
		case TCP_FIN_WAIT_1: /* TODO */
		case TCP_FIN_WAIT_2: /* TODO */
		case TCP_CLOSING: /* TODO */
		default:
			break;
		}
		break;

	case TCP_LAST_ACK:
	case TCP_TIME_WAIT:
		break;

	default:
		assert_not_reached();
	}

	return tcp_handle_segment_text(fsm, seg, ev->seg_len, payload_skip);

discard:
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
		log_dbg("[%p4:%d] %s -> %s", &tsock->isock.addr, tsock->isock.port,
			tcp_state_name(tsock->fsm.cur_state), tcp_state_name(state));
		fsm_set_state(&tsock->fsm, state);
	}
}

// clang-format off

const struct fsm_state tcp_fsm_states[] = {
	[TCP_CLOSED] =		{ tcp_closed,   tcp_enter_closed,       tcp_exit_closed },
	[TCP_LISTEN] =		{ tcp_listen,   NULL,                   NULL },
	[TCP_SYN_SENT] =	{ tcp_syn_sent, tcp_enter_syn_sent,     NULL },
	[TCP_SYN_RECEIVED] =	{ tcp_others,   tcp_enter_syn_received, NULL },
	[TCP_ESTABLISHED] =	{ tcp_others,   tcp_enter_established,  NULL },
	[TCP_FIN_WAIT_1] =	{ tcp_others,   NULL,                   NULL },
	[TCP_FIN_WAIT_2] =	{ tcp_others,   NULL,                   NULL },
	[TCP_CLOSE_WAIT] =	{ tcp_others,   NULL,                   NULL },
	[TCP_CLOSING] =		{ tcp_others,   NULL,                   NULL },
	[TCP_LAST_ACK] =	{ tcp_others,   NULL,                   NULL },
	[TCP_TIME_WAIT] =	{ tcp_others,   tcp_enter_timewait,     tcp_exit_timewait },
};
