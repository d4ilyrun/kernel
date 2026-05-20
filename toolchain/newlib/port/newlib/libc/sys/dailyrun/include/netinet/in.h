/*
 * based on POSIX-1.2024
 */

#ifndef _NETINET_IN_H
#define	_NETINET_IN_H

#include <inttypes.h>
#include <sys/socket.h>

typedef uint16_t in_port_t;
typedef uint32_t in_addr_t;

#define	INET_ADDRSTRLEN		16
#define	INET6_ADDRSTRLEN	46

struct in_addr {
    in_addr_t s_addr;
};

struct sockaddr_in {
    sa_family_t     sin_family;  // AF_INET.
    in_port_t       sin_port;    // Port number. (network byte order)
    struct in_addr  sin_addr;    // IP address.  (network byte order)
};

struct in6_addr {
    uint8_t s6_addr[16];
};

struct sockaddr_in6 {
    sa_family_t      sin6_family;    // AF_INET6.
    in_port_t        sin6_port;      // Port number.  (network byte order)
    uint32_t         sin6_flowinfo;  // IPv6 traffic class and flow information.
    struct in6_addr  sin6_addr;      // IPv6 address. (network byte order)
    uint32_t         sin6_scope_id;  // Set of interfaces for a scope.
};

#define IPPROTO_ICMP 1      /* Internet Control Message Protocol.  */

/* Address to accept any incoming messages.  */
#define INADDR_ANY          ((in_addr_t) 0x00000000)
/* Address to send to all hosts.  */
#define INADDR_BROADCAST    ((in_addr_t) 0xffffffff)

#endif /* _NETINET_IN_H */
