#ifndef UAPI_KERNEL_NET_H
#define UAPI_KERNEL_NET_H

#include <uapi/kernel/net/ip.h>

#include <stdint.h>
#include <stddef.h>

/* TODO: uapi/types.h */
typedef uint16_t sa_family_t;
typedef size_t socklen_t;

/** Supported communication domains */
enum communication_domain {
    AF_UNSPEC = 0, /*!< Unspecfied */
    AF_INET = 2    /*!< IPv4 */
};

/** Supported socket types */
enum socket_type {
    SOCK_STREAM = 0,
    SOCK_DGRAM = 1,
    SOCK_RAW = 3,
};

#define PF_INET AF_INET

struct sockaddr {
    sa_family_t sa_family;
    char sa_data[14]; /* minimum 14 bytes of data */
};

struct sockaddr_in {
    sa_family_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
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

struct cmsghdr {
    socklen_t cmsg_len;
    int cmsg_level;
    int cmsg_type;
    unsigned char cmsg_data[];
};

#endif /* UAPI_KERNEL_NET_H */
