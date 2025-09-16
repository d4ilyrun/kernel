#define LOG_DOMAIN "driver"

#include <kernel/device.h>
#include <kernel/devices/driver.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>

#include <libalgo/linked_list.h>
#include <utils/compiler.h>
#include <utils/container_of.h>
#include <utils/macro.h>

#include <string.h>

/** The list of currently loaded drivers.
 *
 * When trying to match a device against a driver, the driver is looked for
 * inside this list. Every time a new driver is loaded, it is added inside this
 * list.
 *
 * TODO: Use a BST
 * TODO: Use a locking mechanism for when we switch to SMP
 */
static DECLARE_LLIST(loaded_drivers);

extern u32 _kernel_drivers_start;
extern u32 _kernel_drivers_end;

static ALWAYS_INLINE driver_t *to_driver(const node_t *this)
{
    return container_of(this, driver_t, this);
}

void driver_load_drivers(void)
{
    // Inside the 'data.driver.*' section are stored the addresses of ever
    // driver's init function.
    for (driver_init_t *init_function = (void *)&_kernel_drivers_start;
         (void *)init_function < (void *)&_kernel_drivers_end;
         init_function += 1) {
        (*init_function)();
    }
}

void driver_register(driver_t *driver)
{
    log_dbg("loading driver '%s'", driver->name);
    llist_add(&loaded_drivers, &driver->this);
}

error_t driver_probe(driver_t *driver, device_t *device)
{
    error_t status = driver->operations.probe(device);
    if (status) {
        log_variable_str(driver->name);
        log_warn("Failed to probe '%s': %pe", driver->name, &status);
    }

    return status;
}

static int __driver_is_match(const void *this, const void *data)
{
    const driver_t *driver = this;
    return driver->operations.match(this, data) ? COMPARE_EQ : !COMPARE_EQ;
}

driver_t *driver_find_match(device_t *dev)
{
    node_t *driver = llist_find_first(&loaded_drivers, dev, __driver_is_match);
    if (driver == NULL)
        return NULL;

    return to_driver(driver);
}
