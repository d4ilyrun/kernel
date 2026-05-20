/*
 * based on POSIX-1.2024
 */
#ifndef _ARPA_INET_H
#define	_ARPA_INET_H

#include <netinet/in.h>

#include <machine/endian.h>

#define	INET_ADDRSTRLEN		16
#define	INET6_ADDRSTRLEN	46

#define	htonl(x)	__htonl(x)
#define	htons(x)	__htons(x)
#define	ntohl(x)	__ntohl(x)
#define	ntohs(x)	__ntohs(x)

#endif /* _ARPA_INET_H */
