#define LOG_DOMAIN "ethdev"

#include <kernel/devices/ethernet.h>
#include <kernel/atomic.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/net/interface.h>
#include <kernel/net/packet.h>
#include <kernel/sched.h>
#include <kernel/worker.h>

#include <utils/container_of.h>

#include <string.h>
#include <limits.h>

static DECLARE_LLIST(ethernet_devices);
static DECLARE_SPINLOCK(ethernet_devices_lock); // TODO: Use RW lock

static atomic_t ethernet_device_index = { 0 };

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
    struct net_interface *interface;
    struct worker *worker;
    error_t ret;

    device_set_name(&device->device, "eth%u",
                    atomic_inc(&ethernet_device_index));

    worker = kcalloc(1, sizeof(*worker), KMALLOC_KERNEL);
    if (worker == NULL)
        return E_NOMEM;

    ret = worker_init(worker);
    if (ret) {
        log_err("%s: failed to initialize worker thread: %pe",
                ethernet_device_name(device), &ret);
        goto register_failed_free_worker;
    }

    interface = net_interface_new(device, ethernet_device_name(device));
    if (IS_ERR(interface)) {
        ret = ERR_FROM_PTR(interface);
        log_warn("Failed to allocate interface: %pe", &ret);
        goto register_failed_free_worker;
    }

    INIT_LLIST(interface->subnets);
    INIT_QUEUE(device->rx_queue);

    device->interface = interface;
    device->worker = worker;

    spinlock_acquire(&ethernet_devices_lock);
    llist_add(&ethernet_devices, &device->this);
    spinlock_release(&ethernet_devices_lock);
    device_register(&device->device);

    log_info("registered new device: %s", ethernet_device_name(device));

    return E_SUCCESS;

register_failed_free_worker:
    kfree(worker);
    return ret;
}

/*
 *
 */
static inline struct ethernet_device *
ethernet_device_find_by(int (*match)(const void *dev_node, const void *data),
                        const void *data)
{
    node_t *dev_node;

    spinlock_acquire(&ethernet_devices_lock);
    dev_node = llist_find_first(&ethernet_devices, data, match);
    spinlock_release(&ethernet_devices_lock);

    if (!dev_node)
        return NULL;

    return container_of(dev_node, struct ethernet_device, this);
}

/*
 * Find an ethernet device based on its name.
 */
static int __ethernet_device_match_name(const void *dev_node, const void *data)
{
    struct ethernet_device *dev;
    dev = container_of(dev_node, struct ethernet_device, this);
    return strcmp(ethernet_device_name(dev), data);
}

struct ethernet_device *ethernet_device_find_by_name(const char *name)
{
    return ethernet_device_find_by(__ethernet_device_match_name, name);
}

/*
 * Find an ethernet device based on its mac address.
 */
static int __ethernet_device_match_mac(const void *dev_node, const void *data)
{
    struct ethernet_device *dev;
    dev = container_of(dev_node, struct ethernet_device, this);
    return memcmp(dev->mac, data, sizeof(mac_address_t));
}

struct ethernet_device *ethernet_device_find_by_mac(mac_address_t mac)
{
    return ethernet_device_find_by(__ethernet_device_match_mac, mac);
}

/*
 *
 */
static void __ethernet_device_receive_packet(void *cookie)
{
    struct ethernet_device *netdev = cookie;
    struct packet *packet;
    queue_t queue;
    node_t *node;
    error_t err;

    /* Reset the queue so that we can receive new packets while processing */
    no_preemption_scope () {
        INIT_QUEUE(queue);
        queue_enqueue_all(&queue, &netdev->rx_queue);
    }

    while (!queue_is_empty(&queue)) {
        node = queue_dequeue(&queue);
        packet = container_of(node, struct packet, rx_this);
        err = ethernet_receive_packet(packet);
        if (err)
            log_err("failed to handle received packet: %pe", &err);
    }
}

void ethernet_device_receive_packet(struct ethernet_device *netdev,
                                    struct packet *packet)
{
    no_preemption_scope () {
        queue_enqueue(&netdev->rx_queue, &packet->rx_this);
        if (!worker_running(netdev->worker))
            worker_start(netdev->worker, __ethernet_device_receive_packet,
                         netdev);
    }
}
