#ifndef _DAILYRUN_NET_TCP_H
#define _DAILYRUN_NET_TCP_H

#include <stdint.h>
#include <stddef.h>

/*
 * TCP header format.
 *
 * All values are in network order.
 */
struct __attribute__((packed)) tcp_header {
	uint16_t sport;	  /* source port */
	uint16_t dport;	  /* destinatoin port */
	uint32_t seq_num; /* sequence number */
	uint32_t ack_num; /* acknowledgement number */

#ifdef __LITTLE_ENDIAN__
	uint8_t reserved : 4;    /* low nibble first on LE bit-field layout */
	uint8_t data_offset : 4; /* header length in 32-bit words (5..15) */

	uint8_t fin : 1;
	uint8_t syn : 1;
	uint8_t rst : 1;
	uint8_t psh : 1;
	uint8_t ack : 1;
	uint8_t urg : 1;
	uint8_t ece : 1;
	uint8_t cwr : 1;
#else
	uint8_t data_offset : 4; /* high nibble first on BE bit-field layout */
	uint8_t reserved : 4;

	uint8_t cwr : 1;
	uint8_t ece : 1;
	uint8_t urg : 1;
	uint8_t ack : 1;
	uint8_t psh : 1;
	uint8_t rst : 1;
	uint8_t syn : 1;
	uint8_t fin : 1;
#endif

	uint16_t window;   /* network byte order */
	uint16_t checksum; /* network byte order */
	uint16_t urg_ptr;  /* network byte order */
};

static inline size_t tcp_header_size(const struct tcp_header *header)
{
	return header->data_offset * sizeof(uint32_t);
}

static inline size_t tcp_segment_len(const struct tcp_header *seg, size_t payload_size)
{
	size_t segment_len = payload_size;

	/*
	 * The SYN and FIN controls have an implicitely assigned
	 * sequence number.
	 *
	 * SYN is considered to appear before the first actual byte
	 * (seg.seq == SYN if present). FIN is considered to appear
	 * after the last byte.
	 */
	if (seg->syn)
		segment_len += 1;
	if (seg->fin)
		segment_len += 1;

	return segment_len;
}

/* Default MSS values. */
#define TCP_IPV4_MSS 536
#define TCP_IPV6_MSS 1220

/*
 * TCP states.
 */
enum tcp_state {
	TCP_CLOSED,
	TCP_LISTEN,
	TCP_SYN_SENT,
	TCP_SYN_RECEIVED,
	TCP_ESTABLISHED,
	TCP_FIN_WAIT_1,
	TCP_FIN_WAIT_2,
	TCP_CLOSE_WAIT,
	TCP_CLOSING,
	TCP_LAST_ACK,
	TCP_TIME_WAIT,

	/* not an actual TCP state. */
	TCP_STATE_COUNT,
};

/*
 * TCP options.
 */
enum tcp_option {
	/* Mandatory option set (RFC9293) */
	TCP_OPT_END,
	TCP_OPT_NOOP,
	TCP_OPT_MSS, /* TLV, Maximum segment size. */
};

/*
 * TCP TLV option format.
 */
struct tcp_tlv_option {
	uint8_t type;
	uint8_t length;
	uint8_t value[];
};

static inline size_t tcp_header_option_size(const struct tcp_header *header)
{
	return tcp_header_size(header) - sizeof(*header);
}

static inline struct tcp_tlv_option *tcp_header_first_option(const struct tcp_header *header)
{
	if (tcp_header_option_size(header) == 0)
		return NULL;

	return (struct tcp_tlv_option *)((uint8_t *)header + sizeof(*header));
}

static inline struct tcp_tlv_option *tcp_option_next(struct tcp_tlv_option *opt)
{
	uint8_t *next;

	switch (opt->type) {
	case TCP_OPT_END:
		/* No next option */
		return NULL;
	case TCP_OPT_NOOP:
		/* Constant size options */
		next = (uint8_t *)opt + 1;
		break;
	default:
		/* TLV options */
		next = (uint8_t *)opt + sizeof(*opt) + opt->length;
		break;
	}

	return (struct tcp_tlv_option *)next;
}

#endif // !_DAILYRUN_NET_TCP_H
