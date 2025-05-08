/**
 * @brief Network device
 * @file kernel/devices/ethernet.h
 *
 * @defgroup kernel_device_netdev Network Device
 * @ingroup kernel_device
 *
 * # Network Device
 *
 * @{
 */

#ifndef KERNEL_DEVICES_ETHERNET_H
#define KERNEL_DEVICES_ETHERNET_H

#include <kernel/devices/driver.h>
#include <kernel/net/ethernet.h>
#include <kernel/types.h>

#include <libalgo/linked_list.h>
#include <libalgo/queue.h>
#include <utils/compiler.h>
#include <utils/container_of.h>
#include <utils/math.h>

struct ethernet_device;
struct net_interface;

/** Ethernet device capabilities */
enum ethernet_capability {
    ETHERNET_CAP_BROADCAST, /** Device supports broadcast packets */
    ETHERNET_CAP_MULTICAST, /** Device supports multicast packets */
};

/** Operations that can be performed on an ethernet device */
struct ethernet_operations {
    error_t (*send_packet)(struct ethernet_device *, struct packet *);
    error_t (*enable_capability)(struct ethernet_device *,
                                 enum ethernet_capability, bool enable);
};

/** An ethernet device
 *
 * Ethernet devices come with a private data area, whose size is specified
 * by the caller when allocating a new device. This area is destined to contain
 * per-device specific data, generally used to hold the device's state so that
 * it can be used by the driver's functions. This private area is located right
 * after the ethernet_device structure.
 */
struct ethernet_device {
    struct device device;            /** The underlying generic device */
    struct ethernet_operations *ops; /** The ethernet operation vtable */
    mac_address_t mac;               /** The device's mac address */

    size_t mtu; /** Maximum transmittable packet size */

    /** Device capability bitfields @see ethernet_device_capabilities*/
    uint32_t capabilities;

    struct net_interface *interface; /** The netdevice's interface */
    LLIST_NODE(this); /** Node inside the linked list of registered devices */

    struct worker *worker;
    struct queue rx_queue;
};

/** Boundary onto which the ethernet device's private data must be aligned */
#define ETHERNET_DEVICE_PRIV_ALIGNMENT sizeof(uint64_t)

/** Allocate a new ethernet device
 *
 * @param priv_size The size of the device's private data
 *
 * @return The newly created device, or an pointed-encoded error
 */
struct ethernet_device *ethernet_device_alloc(size_t priv_size);

/** De-allocate an ethernet device */
void ethernet_device_free(struct ethernet_device *);

/** Register a new ethernet device */
error_t ethernet_device_register(struct ethernet_device *);

/** @return The matching ethernet device, or NULL */
struct ethernet_device *ethernet_device_find_by_name(const char *);

/** @return The matching ethernet device, or NULL */
struct ethernet_device *ethernet_device_find_by_mac(mac_address_t);

static inline void *ethernet_device_priv(struct ethernet_device *dev)
{
    return (void *)(dev) +
           align_up(sizeof(*dev), ETHERNET_DEVICE_PRIV_ALIGNMENT);
}

/** Set the name of the device */
static inline void
ethernet_device_set_name(struct ethernet_device *dev, const char *name)
{
    device_set_name(&dev->device, name);
}

/** @return The name of the device */
static inline const char *ethernet_device_name(struct ethernet_device *dev)
{
    return device_name(&dev->device);
}

/** Process a packet received by an ethernet network device */
void ethernet_device_receive_packet(struct ethernet_device *, struct packet *);

#endif /* KERNEL_DEVICES_ETHERNET_H */

/** @} */
