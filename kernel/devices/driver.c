#include <kernel/devices/driver.h>

#include <libalgo/linked_list.h>

#include "kernel/logger.h"

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
    log_dbg("driver", "loading driver '%s'", driver->name);
    llist_add(&loaded_drivers, &driver->this);
}

// TODO: Delete this POC driver
static driver_t poc_driver = {
    .name = "POC DRIVER",
};

DECLARE_DRIVER(poc, &poc_driver);
