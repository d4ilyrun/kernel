#ifndef KERNEL_NET_ROUTE_H
#define KERNEL_NET_ROUTE_H

#include <kernel/error.h>
#include <kernel/net.h>

/***/
struct routing_point {
    __be struct sockaddr_in ip; /** IP address */
    struct sockaddr_mac mac;    /** MAC address*/
};

/** Routing structure */
struct net_route {
    struct ethernet_device *netdev; /** Network device */
    struct routing_point src;       /** Source address*/
    struct routing_point dst;       /** Destination address*/
};

/***/
error_t
net_route_compute(struct net_route *route, const struct sockaddr_in *dst);

#endif /* KERNEL_NET_ROUTE_H */
