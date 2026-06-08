/*
 * based on POSIX-1.2024
 */

#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#include <dailyrun/socket.h>

typedef unsigned int sa_family_t;
typedef uint32_t socklen_t;

struct sockaddr {
    sa_family_t sa_family;
    char sa_data[14]; /* minimum 14 bytes of data */
};

struct iovec {      /* Scatter/gather array items */
    void *iov_base; /* Starting address */
    size_t iov_len; /* Number of bytes to transfer */
};

struct msghdr {
    void *msg_name;        /* optional address */
    socklen_t msg_namelen; /* size of address */
    struct iovec *msg_iov; /* scatter/gather array */
    size_t msg_iovlen;     /* # elements in msg_iov */
    void *msg_control;     /* ancillary data, see below */
    size_t msg_controllen; /* ancillary data buffer len */
    int msg_flags;         /* flags on received message */
};

#define MSG_TRUNC 0x1

struct cmsghdr {
    socklen_t cmsg_len;
    int cmsg_level;
    int cmsg_type;
    unsigned char cmsg_data[];
};

#ifndef KERNEL

int     accept(int, struct sockaddr *restrict, socklen_t *restrict);
int     bind(int, const struct sockaddr *, socklen_t);
int     connect(int, const struct sockaddr *, socklen_t);
int     getpeername(int, struct sockaddr *restrict, socklen_t *restrict);
int     getsockname(int, struct sockaddr *restrict, socklen_t *restrict);
int     getsockopt(int, int, int, void *restrict, socklen_t *restrict);
int     listen(int, int);
ssize_t recv(int, void *, size_t, int);
ssize_t recvfrom(int, void *restrict, size_t, int,
        struct sockaddr *restrict, socklen_t *restrict);
ssize_t recvmsg(int, struct msghdr *, int);
ssize_t send(int, const void *, size_t, int);
ssize_t sendmsg(int, const struct msghdr *, int);
ssize_t sendto(int, const void *, size_t, int, const struct sockaddr *,
        socklen_t);
int     setsockopt(int, int, int, const void *, socklen_t);
int     shutdown(int, int);
int     sockatmark(int);
int     socket(int, int, int);
int     socketpair(int, int, int, int [2]);

#endif /* !KERNEL */

#endif /* _SYS_SOCKET_H */
