#ifndef _KERNEL_NET_TCP_H
#define _KERNEL_NET_TCP_H

#include <kernel/net/ipv4.h>
#include <kernel/socket.h>
#include <kernel/types.h>

#include <dailyrun/net/tcp.h>

#include <libalgo/hashtable.h>
#include <libalgo/ringbuffer.h>
#include <libfsm.h>
#include <utils/constants.h>

/* Use the Maximum Segment Lifetime value defined in RFC9293. */
#define TCP_MSL 120

extern struct socket_protocol_ops af_inet_tcp_ops;

error_t tcp_receive_packet(struct packet *packet);

extern const struct fsm_state tcp_fsm_states[];

enum tcp_event {
	TCP_EV_SEGMENT_ARRIVES,
	TCP_EV_TIMEOUT,
};

struct tcp_fsm_ev {
	enum tcp_event type;
	struct tcp_sock *tsock;
	union {
		/* For 'Segment arrives' events. */
		struct {
			struct tcp_header *seg;
			size_t seg_len;
		};
	};
};

void tcp_set_state(struct tcp_sock *tsock, unsigned int state);

/*
 * TCP transmission control block.
 *
 * Holds the state of a TCP connection.
 *
 * Local and peer address/port, as defined inside RFC9293,
 * are kept track of inside the socket's isock field.
 *
 * @see RFC9293 - 3.3.1
 */
struct tcb {
	u32 iss; /* initial send sequence number */
	u32 irs; /* initial receive sequence number */

	/* Track wether this connection was opened as a passive or active one. */
	enum tcp_state initial_state;

	struct {
		/* Sequence numbers before unack have already been acknowledged
		 * by the remote peer. Numbers between unack and next have been sent
		 * and are waiting to be acknowledged. Numbers betewen next and
		 * (unack + window_size) are valid sequence numbers for new data
		 * transmissions.
		 *
		 * @see Figure 3.
		 */
		u32 unack;
		u32 next;
		u32 window_size;
		u32 window_l1; /* sequence number used for last window update */
		u32 window_l2; /* acknowledgment number used for last window update */

		/* Maximum segment size supported by the peer. */
		unsigned int mss;

		/**/
		u32 max_window_size;
	} send;

	struct {
		/* Sequence numbers before next have already been acknowledged by us.
		 * Those beteween next and (next + window_size) are allowed for reception.
		 */
		u32 next;
		u32 window_size;
	} recv;
};

u32 tcp_compute_isn(void);

/* Receive window control */
bool tcp_window_accept(const struct tcb *, u32 seq, size_t seg_len);
bool tcp_window_receive(struct tcb *, u32 seq, size_t seg_len);
void tcp_window_trim_segment(struct tcb *, struct tcp_header *seg, size_t *out_seg_len,
			     size_t *out_payload_skip);

/* Send window control */
bool tcp_window_send(struct tcb *, u32 *seq, size_t seg_len);
bool tcp_window_accept_ack(struct tcb *, u32 seq);
bool tcp_window_ack_is_duplicate(struct tcb *, u32 seq);
bool tcp_window_ack(struct tcb *, u32 seq);
bool tcp_window_update_send(struct tcb *, size_t window, u32 seq, u32 ack);

#define TCP_DEFAULT_BUFFER_SIZE (32 * KB)

/*
 * Private TCP socket data.
 */
struct tcp_sock {
	struct inet_sock isock;
	struct socket *socket;
	struct fsm fsm;
	struct tcb tcb;
	struct hashtable_entry hash;
	struct timeout *time_wait_timeout;
	struct ringbuffer buffer;
	size_t buffered_bytes;
	llist_t xmit_queue;
};

void tcp_send_pending_segments(struct tcp_sock *tsock);

/* Send simple control blocks. */
error_t tcp_send_rst(struct tcp_sock *, unsigned int seq);
error_t tcp_send_ack(struct tcp_sock *);
error_t tcp_send_syn(struct tcp_sock *, bool ack);

#define FOREACH_TCP_OPTION(_opt, _seg)                                              \
	for (_opt = tcp_header_first_option(_seg);                                  \
	     _opt && (size_t)((void *)_opt - (void *)_seg) < tcp_header_size(_seg); \
	     _opt = tcp_option_next(_opt))

#endif /* _KERNEL_NET_TCP_H */
