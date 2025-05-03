#ifndef UAPI_KERNEL_NET_IP_H
#define UAPI_KERNEL_NET_IP_H

/** Values for the protocol field inside the IPv4 header
 *
 *  These values are also used as the protocol number for
 *  AF_INET sockets.
 *
 *  @enum ip_protocol
 */
enum ip_protocol {
    IPPROTO_IP = 0,   /*!< Dummy protocol */
    IPPROTO_TCP = 6,  /*!< TCP */
    IPPROTO_UDP = 17, /*!< UDP */
};

#endif /* UAPI_KERNEL_NET_IP_H */
