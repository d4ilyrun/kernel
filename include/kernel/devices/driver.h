/**
 * @brief Driver API
 *
 * @defgroup kernel_device_driver Drivers
 * @ingroup kernel_device
 *
 * # Drivers
 *
 * This file describes the **driver** part of the Device/Driver API.
 *
 * ## Creating a driver
 *
 * When creating a new driver, you MUST declare it using \ref DECLARE_DRIVER.
 * All drivers declared using this macro are automatically loaded when starting
 * up the kernel.
 *
 * The kernel automatically detects and keeps track of all the connected
 * hardware devices. When a new device is detected, its corresponding driver is
 * looked for inside this list of loaded drivers. If a match is found, the
 * matching driver's \c probe function is called to create the correct type of
 * device, and register it.
 *
 * ## Driver types
 *
 * Drivers for devices on a same bus generally share some similarities, at least
 * in the way the are interacted with. The driver struct should be embedded
 * inside a more specific 'per-bus' struct and interacted with through the bus's
 * API.
 *
 * @see kernel_device
 *
 * @{
 */

#pragma once

#include <kernel/device.h>
#include <kernel/error.h>

#include <libalgo/linked_list.h>
#include <utils/compiler.h>

/** The basic device driver structure
 *
 * This structure defines the common base interface used to enumerate and
 * interact with all device drivers. It should be embedded inside a more
 * specific 'per-bus' struct, and interacted with through the bus's API.
 *
 * Example:
 *
 * ```c
 *
 * struct pci_driver {
 *     struct device_driver driver;
 *     void *pci_specific_field_1;
 *     int pci_specific_field_2;
 * };
 *
 * void pci_driver_register(struct pci_driver *driver)
 * {
 *     // Perform pci-specific actions
 *     // ...
 *
 *     // Register driver generically
 *     driver_register(&driver->driver);
 * }
 *
 * ```
 *
 * To interact with a \c pci_device, we will interact with the \c pci_*
 * functions. These functions will be the one to interact with the \c driver_*
 * api instead, adding into the process the steps/configuration necessary for
 * PCI devices to work.
 *
 * @struct device_driver
 */
typedef struct device_driver {

    node_t this; ///< Intrusive list node used to iterate through loaded drivers
    const char *name; ///< The name of the driver

    /** Vector table of the common operations used to control drivers
     *  @struct driver_operations
     */
    struct driver_operations {
        /// Bind the driver to a device
        error_t (*probe)(device_t *);
        /// Check if the driver should be used for this device
        /// This function should be the same for each driver on the same bus
        /// (PCI, ACPI, ...). Typically inserted by the per-bus driver API.
        bool (*match)(const driver_t *, const device_t *);
    } operations; ///< Vector table of driver control operations

} driver_t;

/** Register a new driver
 *
 * This function adds the driver to the list of enumeratable drivers.
 * It should **always** be called inside a `per-bus` API's register
 * function as this step is bus-independent (and required).
 *
 * After a driver has been registerd, any newly detected device that it matches
 * will automatically be bound to it using the \c probe function.
 *
 * @info This function is generally called automatically during startup on the
 * declared drivers, and should not need to be called manually elsewhere.
 */
void driver_register(driver_t *driver);

error_t driver_probe(driver_t *driver, device_t *device);

typedef void (*driver_init_t)(void);

/** Declare a new driver.
 *
 * Drivers declared using this macro are automatically loaded at startup, and
 * are automatically associated with their corresponding devices.
 *
 * @param _name The name of the driver
 * @param _driver The driver's definition (\ref device_driver)
 * @param _driver_register The per-bus API register function
 */
#define DECLARE_DRIVER(_name, _driver, _driver_register) \
    static void init_driver_##_name(void)                \
    {                                                    \
        _driver_register(_driver);                       \
    }                                                    \
                                                         \
    SECTION(".data.driver.init")                         \
    MAYBE_UNUSED                                         \
    static driver_init_t __##_name##_driver_init = init_driver_##_name;

/** Load all builtin drivers.
 *  Builtin drivers are ones declared using \ref DECLARE_DRIVER
 */
void driver_load_drivers(void);

/** Retreive the driver that matches the given arguments.
 *  @return A pointer to the driver, or one containing an eventual error code.
 */
driver_t *driver_find_match(device_t *);

/** @} */
