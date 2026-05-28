#define LOG_PREFIX "dev"

#include <kernel/device.h>
#include <kernel/error.h>
#include <kernel/logger.h>
#include <kernel/spinlock.h>

#include <libalgo/linked_list.h>
#include <utils/container_of.h>

#include <string.h>

/** Global list of currently registered devices */
DECLARE_LLIST(registered_devices);
DECLARE_SPINLOCK(registered_devices_lock); // TODO: Use RW-lock

/*
 * Make sure that a device's name is valid before registering it.
 */
static error_t device_check_name(const struct device *new)
{
    struct device *existing;

    ASSERT(spinlock_is_held(&registered_devices_lock));

    if (!new->name)
        return E_INVAL;

    FOREACH_LLIST_ENTRY(existing, &registered_devices, this) {
        if (!strcmp(existing->name, new->name)) {
            return E_EXIST;
        }
    }

    return E_SUCCESS;
}

/*
 * Add new device to the list of existing devices.
 */
error_t device_register(device_t *dev)
{
    error_t err;

    spinlock_acquire(&registered_devices_lock);

    err = device_check_name(dev);
    if (err)
        goto out;

    dev->vnode = NULL;
    llist_add(&registered_devices, &dev->this);
    err = E_SUCCESS;

out:
    spinlock_release(&registered_devices_lock);
    return err;
}

/*
 *
 */
struct device *device_find(const char *name)
{
    struct device *dev;
    bool found = false;

    locked_scope (&registered_devices_lock) {
        FOREACH_LLIST (node, &registered_devices) {
            dev = container_of(node, device_t, this);
            if (!strcmp(dev->name, name)) {
                found = true;
                break;
            }
        }
    }

    if (!found)
        return NULL;

    return dev;
}
