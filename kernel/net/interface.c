#define LOG_DOMAIN "netif"

#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/net.h>
#include <kernel/net/arp.h>
#include <kernel/net/interface.h>

#include <libalgo/linked_list.h>
#include <utils/bits.h>

static DECLARE_LLIST(registered_net_interfaces);

struct net_interface *
net_interface_new(struct ethernet_device *netdev, const char *name)
{
    struct net_interface *interface;

    interface = kcalloc(1, sizeof(struct net_interface), KMALLOC_KERNEL);
    if (interface == NULL) {
        log_warn("failed to allocate new interface: %s", name);
        return PTR_ERR(E_NOMEM);
    }

    interface->netdev = netdev;
    interface->name = name;

    llist_add(&registered_net_interfaces, &interface->this);

    return interface;
}

error_t net_interface_add_subnet(struct net_interface *interface,
                                 __be ipv4_t addr, uint8_t cidr)
{
    struct subnet *subnet;

    if (interface == NULL)
        return E_INVAL;

    /* CIDR must not be larger than the address's size (32 for ipv4) */
    if (cidr > 8 * sizeof(addr))
        return E_INVAL;

    subnet = kmalloc(sizeof(struct subnet), KMALLOC_KERNEL);
    if (subnet == NULL)
        return E_NOMEM;

    subnet->interface = interface;
    subnet->ip = addr;
    subnet->cidr = cidr;

    arp_add(subnet->ip, interface->netdev->mac);

    llist_add(&interface->subnets, &subnet->this);

    return E_SUCCESS;
}

static int __subnet_match_ip(const void *node, const void *data)
{
    struct subnet *subnet = container_of(node, struct subnet, this);
    ipv4_t addr = ntohl((__be ipv4_t)data);
    ipv4_t match = ntohl(subnet->ip);

    addr = align_down(addr, BIT(32 - subnet->cidr));
    match = align_down(match, BIT(32 - subnet->cidr));

    return (addr == match) ? COMPARE_EQ : !COMPARE_EQ;
}

const struct subnet *net_interface_find_subnet(__be ipv4_t addr)
{
    struct net_interface *interface;
    node_t *match = NULL;

    FOREACH_LLIST (if_node, registered_net_interfaces) {
        interface = container_of(if_node, struct net_interface, this);
        match = llist_find_first(interface->subnets, (void *)addr,
                                 __subnet_match_ip);
        if (match)
            break;
    }

    if (match == NULL)
        return NULL;

    return container_of(match, struct subnet, this);
}
