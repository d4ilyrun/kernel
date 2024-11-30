#include <kernel/devices/ethernet.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/net/interface.h>

#include <utils/container_of.h>

#include <string.h>

DECLARE_LLIST(ethernet_registered_devices);

struct ethernet_device *ethernet_device_alloc(size_t priv_size)
{
    struct ethernet_device *device;
    size_t total_size;

    total_size = priv_size + align_up(sizeof(struct ethernet_device),
                                      ETHERNET_DEVICE_PRIV_ALIGNMENT);

    device = kcalloc(1, total_size, KMALLOC_KERNEL);
    if (device == NULL)
        return PTR_ERR(E_NOMEM);

    return device;
}

void ethernet_device_free(struct ethernet_device *device)
{
    kfree(device);
}

error_t ethernet_device_register(struct ethernet_device *device)
{
    /* TODO: kasprintf("eth%d", registered_devices++) */
    struct net_interface *interface;
    const char *name = "eth0";

    if (!ethernet_device_name(device))
        ethernet_device_set_name(device, name);

    interface = net_interface_new(device, ethernet_device_name(device));
    if (IS_ERR(interface)) {
        log_warn("ethdev", "Failed to allocate interface");
        return ERR_FROM_PTR(interface);
    }

    device->interface = interface;

    llist_add(&ethernet_registered_devices, &device->this);
    device_register(&device->device);

    return E_SUCCESS;
}

static int __ethernet_device_match_name(const void *dev_node, const void *data)
{
    struct ethernet_device *dev;
    dev = container_of(dev_node, struct ethernet_device, this);
    return strcmp(ethernet_device_name(dev), data);
}

static int __ethernet_device_match_mac(const void *dev_node, const void *data)
{
    struct ethernet_device *dev;
    dev = container_of(dev_node, struct ethernet_device, this);
    return memcmp(dev->mac, data, sizeof(mac_address_t));
}

struct ethernet_device *ethernet_device_find_by_name(const char *name)
{
    node_t *dev_node = llist_find_first(ethernet_registered_devices, name,
                                        __ethernet_device_match_name);
    if (!dev_node)
        return NULL;

    return container_of(dev_node, struct ethernet_device, this);
}

struct ethernet_device *ethernet_device_find_by_mac(mac_address_t mac)
{
    node_t *dev_node = llist_find_first(ethernet_registered_devices, mac,
                                        __ethernet_device_match_mac);
    if (!dev_node)
        return NULL;

    return container_of(dev_node, struct ethernet_device, this);
}
