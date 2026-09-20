/*
 * TCP protocol - congestion window control.
 *
 * ## References
 *
 * - RFC 9293 - Transmission Control Protocol (TCP)
 */
#include <kernel/net/tcp.h>
#include <kernel/logger.h>

/*
 * @return whether the segment acknowledgement is acceptable.
 */
bool tcp_window_accept_ack(struct tcb *tcb, u32 seq)
{
	return tcb->send.unack < seq && seq <= tcb->send.next;
}

/*
 *
 */
bool tcp_window_ack_is_duplicate(struct tcb *tcb, u32 seq)
{
	return seq <= tcb->send.unack;
}

/*
 * Acknowledge previously sent sequence numbers.
 *
 * @return false if the acknowledged sequence number is unacceptable.
 */
bool tcp_window_ack(struct tcb *tcb, u32 seq)
{
	if (!tcp_window_accept_ack(tcb, seq))
		return false;

	tcb->send.unack = seq;

	return true;
}

/*
 * Request a number of sequence numbers from the current window before
 * sending a segment.
 *
 * @return false if the current number of available sequence numbers is smaller
 *         than what was requested.
 */
bool tcp_window_send(struct tcb *tcb, u32 *seq, size_t seg_len)
{
	u32 unack_size;
	u32 usable;

	/* compute usable window size */
	unack_size = tcb->send.next - tcb->send.unack;
	ASSERT(unack_size <= tcb->send.window_size);
	usable = tcb->send.window_size - unack_size;
	if (seg_len > usable)
		return false;

	*seq = tcb->send.next;
	tcb->send.next += seg_len;

	return true;
}

/*
 * Update the size of the send window.
 */
bool tcp_window_update_send(struct tcb *tcb, size_t window, u32 seq, u32 ack)
{
	if (seq < tcb->send.window_l1)
		return false;
	if (seq == tcb->send.window_l1 && ack < tcb->send.window_l2 )
		return false;

	tcb->send.window_size = window;
	tcb->send.window_l1 = seq;
	tcb->send.window_l2 = ack;

	if (window > tcb->send.max_window_size)
		tcb->send.max_window_size = window;

	return true;
}

static inline bool seq_in_window(const struct tcb *tcb, u32 seq)
{
	return (u32)(seq - tcb->recv.next) < tcb->recv.window_size;
}

/*
 * @return true if a TCP segment is acceptable based on sequence numbers.
 */
bool tcp_window_accept(const struct tcb *tcb, u32 seq, size_t seg_len)
{
	u32 last_seq = seq + seg_len - 1;

	if (tcb->recv.window_size == 0)
		return seg_len == 0 && seq == tcb->recv.next;

	if (seq_in_window(tcb, seq))
		return true;

	return seg_len > 0 && seq_in_window(tcb, last_seq);
}

/*
 * Update the receive window after reception of a segment.
 *
 * @return False if the segment cannot be accepted.
 */
bool tcp_window_receive(struct tcb *tcb, u32 seq, size_t seg_len)
{
	u32 last_seq = seq + seg_len - 1;
	size_t received;
	u32 next;

	if (!tcp_window_accept(tcb, seq, seg_len))
		return false;

	next = MIN(last_seq + 1, tcb->recv.next + tcb->recv.window_size);
	received = next - tcb->recv.next;

	tcb->recv.user += received;
	tcb->recv.window_size -= received;
	tcb->recv.next = next;

	return true;
}

/*
 * Update the receive window after a buffered segment was read by the user.
 *
 * @see 3.8.6.2.2. SWS - Receiver's Algorithm
 */
void tcp_window_user_receive(struct tcb *tcb, size_t len)
{
	struct tcp_sock *tsock = container_of(tcb, struct tcp_sock, tcb);
	struct tcp_header hdr;
	unsigned int mss = 0;
	size_t unused;

	tcb->recv.user -= len;

	memset(&hdr, 0, sizeof(hdr));
	mss = tcp_effective_mss(tsock, &hdr);

	unused = tcb->recv.buff - tcb->recv.user - tcb->recv.window_size;
	if (unused >= MIN(tcb->recv.buff / 2, mss))
		tcb->recv.window_size = tcb->recv.buff - tcb->recv.user;
}

/*
 * Update a segment's sequence number and size to make it fit the receive window perfectly
 * (i.e. starts at RCV.NXT and does not exceed the window).
 *
 * NOTE: We assume that the given segment is acceptable. The caller should always call
 *       tcp_window_accept() to verify this assumption before calling this function.
 */
void tcp_window_trim_segment(struct tcb *tcb, struct tcp_header *seg,
			     size_t *out_seg_len, size_t *out_payload_skip)
{
	size_t seg_len = *out_seg_len;
	u32 seq = ntohl(seg->seq_num);
	u32 last_seq = seq + seg_len - 1;
	size_t already_acked = 0;
	size_t outside_window = 0;

	/* nothing to trim */
	if (!seg_len)
		goto out;

	/* These computations only work on the premise that the segment is acceptable. */
	if (!seq_in_window(tcb, seq))
		already_acked = tcb->recv.next - seq;
	if (!seq_in_window(tcb, last_seq))
		outside_window = last_seq - (tcb->recv.next + tcb->recv.window_size) + 1;

	/* Trim already acknowledged sequence numbers. */
	if (already_acked > 0) {
		if (seg->syn) {
			/* If present, the SYN flag has a sequence number assigned, and is
			 * present before the first byte of actual data in the segment. */
			seg->syn = false;
			seq += 1;
			seg_len -= 1;
			already_acked -= 1;
		}
		seq += already_acked;
		seg_len -= already_acked;
	}

	/* Trim sequence numbers outside of the receive window. */
	if (outside_window > 0) {
		if (seg->fin) {
			/* Same as for SYN, but the FIN flag is always the last sequence number
			 * in a segment. */
			seg->fin = false;
			seg_len -= 1;
			outside_window -= 1;
		}
		seg_len -= outside_window;
	}

out:
	seg->seq_num = htonl(seq);
	*out_seg_len = seg_len;
	*out_payload_skip = already_acked;
}
