/**
 * @file kernel/net/interface.h
 * @brief Network Interfaces
 *
 * @defgroup networking_interface Network Interfaces
 * @ingroup networking
 *
 * # Interfaces
 *
 * Network interfaces are a layer just above the network device,
 * used for packet routing. It associates subnets to network devices,
 * allowing us to retreive which network device should be used
 * depending on the destination address.
 *
 * An interface can correspond to only ONE network device, but can
 * have multiple subnet masks.
 *
 * ## Routing - Subnet
 *
 * A subnet is used to match the **destination** IP when sending a packet.
 *
 * Each subnet contains a source IP, and a subnet mask. If the destination
 * is in the subnet's range, then its source IP is used as the packet's
 * source address and the packet is sent through the subnet's containing
 * interface's network device.
 *
 * @{
 */

#ifndef KERNEL_NET_INTERFACE_H
#define KERNEL_NET_INTERFACE_H

#include <kernel/devices/ethernet.h>
#include <kernel/types.h>

#include <libalgo/linked_list.h>

/** A network interface */
struct net_interface {
    const char *name;               /** The name of this interface */
    struct ethernet_device *netdev; /** The interface's network device */
    llist_t subnets; /** The subnets registered under this interface */
    LLIST_NODE(this);
};

/** An interface subnet */
struct subnet {
    LLIST_NODE(this);
    struct net_interface *interface; /** The subnet's containing interface */
    ipv4_t ip;    /** source ip used when interacting with this subnet */
    uint8_t cidr; /** subnet mask prefix length */
};

/** Create a new network interface */
struct net_interface *net_interface_new(struct ethernet_device *, const char *);

/** Add a new subnet to a network interface */
error_t net_interface_add_subnet(struct net_interface *, ipv4_t, uint8_t cidr);

/** Find the first subnet that contains the specified address
 * @return The matching subnet or NULL
 */
const struct subnet *net_interface_find_subnet(ipv4_t);

#endif /* KERNEL_NET_INTERFACE_H */

/** @} */
