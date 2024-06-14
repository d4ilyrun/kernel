#include <kernel/device.h>

/** Global list of currently registered devices */
DECLARE_LLIST(registered_devices);

error_t device_register(device_t *dev)
{
    llist_add(&registered_devices, &dev->this);
    return E_SUCCESS;
}
