#include <kernel/net/arp.h>
#include <kernel/net/interface.h>
#include <kernel/net/route.h>

#include <string.h>

error_t net_route_compute(struct net_route *route, const struct sockaddr_in *dst)
{
    const struct subnet *subnet;
    const mac_address_t *daddr_mac;

    subnet = net_interface_find_subnet(dst->sin_addr);
    if (!subnet)
        return E_NET_UNREACHABLE;

    daddr_mac = arp_get(dst->sin_addr);
    if (daddr_mac == NULL)
        /* TODO: ARP request */
        return E_NET_UNREACHABLE;

    route->netdev = subnet->interface->netdev;

    route->dst.ip = *dst;
    memcpy(route->dst.mac.mac_addr, daddr_mac, sizeof(mac_address_t));

    /* Use same port and family for source */
    route->src.ip = *dst;
    route->src.ip.sin_addr = subnet->ip;
    memcpy(route->src.mac.mac_addr, subnet->interface->netdev->mac,
           sizeof(mac_address_t));

    return E_SUCCESS;
}
