#ifndef _DAILYRUN_SOCKET_H
#define _DAILYRUN_SOCKET_H

/* Socket communication domains */
#define AF_UNSPEC 0 // Unspecified
#define AF_INET   2 // IPv4

/* Socket types */
#define SOCK_STREAM 0
#define SOCK_DGRAM  1
#define SOCK_RAW    3

typedef unsigned int socket_type_t;

#endif /* _DAILYRUN_SOCKET_H */
