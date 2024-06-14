#include <kernel/devices/driver.h>
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
    log_dbg("driver", "loading driver '%s'", driver->name);
    llist_add(&loaded_drivers, &driver->this);
}

static int driver_is_match(const void *this, const void *data)
{
    driver_t *driver = to_driver(this);
    const struct driver_match *match = data;

    if (driver->match.method != match->method)
        return COMPARE_EQ;

    for (int i = 0; driver->match.compatible[i]; i++) {
        if (!strcmp(driver->match.compatible[i], match->compatible[0]))
            return COMPARE_EQ;
    }

    return !COMPARE_EQ;
}

const driver_t *driver_find_match(device_detection_method method,
                                  const char *data)
{
    const char *compatible[] = {data, NULL};
    struct driver_match match = {
        .compatible = compatible,
        .method = method,
    };

    node_t *driver = llist_find_first(loaded_drivers, &match, driver_is_match);
    if (driver == NULL)
        return NULL;

    return to_driver(driver);
}

// TODO: Delete this POC driver

static const char *poc_compatible[] = {
    "PNP0103",
    "PNP0303",
    NULL,
};

static error_t poc_probe(const char *name, paddr_t addr)
{
    log_dbg("poc-driver", "probing %s@" LOG_FMT_32, name, addr);
    return E_SUCCESS;
}

static driver_t poc_driver = {
    .name = "poc-driver",
    .operations.probe = poc_probe,
    .match =
        {
            .method = DRIVER_TYPE_ACPI,
            .compatible = poc_compatible,
        },
};

DECLARE_DRIVER(poc, &poc_driver);
