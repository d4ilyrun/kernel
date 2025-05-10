#ifndef KERNEL_NET_ICMP_H
#define KERNEL_NET_ICMP_H

#include <kernel/error.h>
#include <kernel/net.h>
#include <kernel/socket.h>
#include <kernel/types.h>

extern struct socket_protocol_ops af_inet_icmp_ops;

/** ICMP frame header format */
struct icmp_header {
    u8 type;
    u8 code;
    __be u16 checksum;
};

#define ICMP_HEADER_SIZE 4
static_assert(sizeof(struct icmp_header) == ICMP_HEADER_SIZE);

/** Content of the ICMP header's 'type' field */
enum icmp_type {
    ICMP_ECHO_REPLY = 0,   /* Ping reply */
    ICMP_ECHO_REQUEST = 8, /* Ping request */
};

/** Process a newly received ICMP packet */
error_t icmp_receive_packet(struct packet *packet);

#endif /* KERNEL_NET_ICMP_H */
