#include <kernel/net/arp.h>
#include <kernel/net/interface.h>
#include <kernel/net/route.h>

#include <string.h>

error_t net_route_compute(struct net_route *route, const struct sockaddr_in *dst)
{
	const struct subnet *subnet;
	const mac_address_t *daddr_mac;

	/* IPv6 not supported */
	if (dst->sin_family != AF_INET)
		return E_NET_UNREACHABLE;

	subnet = net_interface_find_subnet(dst->sin_addr.s_addr);
	if (!subnet)
		return E_NET_UNREACHABLE;
	route->netdev = subnet->interface->netdev;

	daddr_mac = arp_get(dst->sin_addr.s_addr);
	if (!daddr_mac) {
		daddr_mac = arp_request(dst->sin_addr.s_addr);
		if (!daddr_mac)
			return E_NET_UNREACHABLE;
	}

	memcpy(&route->dst.ip, dst, sizeof(route->dst.ip));
	memcpy(route->dst.mac.mac_addr, daddr_mac, sizeof(mac_address_t));

	route->src.ip.sin_family = dst->sin_family;
	route->src.ip.sin_addr.s_addr = subnet->ip;
	memcpy(route->src.mac.mac_addr, subnet->interface->netdev->mac, sizeof(mac_address_t));

	return E_SUCCESS;
}
