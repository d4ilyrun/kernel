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
 * @see kernel_device
 *
 * @{
 */

#pragma once

#include <kernel/device.h>
#include <kernel/error.h>

#include <libalgo/linked_list.h>
#include <utils/compiler.h>

/** The different ways a device can be detected
 *  @enum device_detection_method
 */
typedef enum device_detection_method {
    DRIVER_TYPE_ACPI, ///< Devices detected through ACPI tables
    DRIVER_TYPE_TOTAL_COUNT,
} device_detection_method;

/** The basic device driver structure
 *  @struct device_driver
 */
typedef struct device_driver {

    node_t this; ///< Intrusive list node used to iterate through loaded drivers
    const char *name; ///< The name of the driver

    /** Information used by the driver API to match a device against a driver
     *  @struct driver_match
     */
    struct driver_match {
        /// How matching devices are expected to be detected
        device_detection_method method;
        /// The matching data.
        /// This can be a name, a path or anything depending on the detection
        /// method.
        const char *const *compatible;
    } match; ///< Information used to match devices against this driver

    /** Vector table of the common operations used to control drivers
     *  @struct driver_operations
     */
    struct driver_operations {
        /// Bind the driver to the device
        /// @param name The name of the matching physical device
        /// @param addr The physical address of the device
        error_t (*probe)(const char *, paddr_t addr);
    } operations; ///< Vector table of driver control operations

} driver_t;

/** Register a new driver
 *
 * After a driver has been registerd, any newly detected device that it matches
 * will automatically be bound to it using the \c probe function.
 *
 * @info This function is generally called automatically during startup on the
 * declared drivers, and should not need to be called manually elsewhere.
 */
void driver_register(driver_t *driver);

typedef void (*driver_init_t)(void);

/** Declare a new driver.
 *
 * Drivers declared using this macro are automatically loaded at startup, and
 * are automatically associated with their corresponding devices.
 *
 * @param _name The name of the driver
 * @param _driver The driver's definition (\ref device_driver)
 */
#define DECLARE_DRIVER(_name, _driver)    \
    static void init_driver_##_name(void) \
    {                                     \
        driver_register(_driver);         \
    }                                     \
                                          \
    SECTION(".data.driver.init")          \
    MAYBE_UNUSED                          \
    static driver_init_t __##_name##_driver_init = init_driver_##_name;

/** Load all builtin drivers.
 *  Builtin drivers are ones declared using \ref DECLARE_DRIVER
 */
void driver_load_drivers(void);

/** Retreive the driver that matches the given arguments.
 *  @return A pointer to the driver, or one containing an eventual error code.
 */
const driver_t *driver_find_match(device_detection_method, const char *);

/** @} */
