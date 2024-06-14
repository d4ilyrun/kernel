#pragma once

/**
 *
 * @brief Kernel Device/Driver API
 *
 * @defgroup kernel_device Devices
 * @ingroup kernel
 *
 * # Devices
 *
 * ## Philsophy
 *
 * This device driver API is heavily inspired by Linux's one, as it is the only
 * one I'm familiar with currently, and I like it.
 *
 * Hardware interactions are split into two 3 parts:
 * * The physical hardware (not our responsibility)
 * * The driver
 * * The device
 *
 * **Every interaction with a hardware component is done by interacting with its
 * corresponding device.**
 *
 *
 * ## Design
 *
 * Devices come in many shapes (timers, buses, etc ...), and as such, the sets
 * of functions used to communicate with a given hardware varies a lot.
 * The different function groups (vtables) should be located inside the
 * \c device struct (whichever one, depending on the type of hardware),
 * and should stay hidden the driver.
 *
 * The driver's only responsibility is to create, initialize and register the
 * appropriate device \ref device for its hardware. Once the device has been
 * registered, it can be used to communicate with the underlying hardware.
 *
 *
 * ## Usage
 *
 * The kernel automatically detects and keeps track of all the connected
 * hardware devices. When a new device is detected, its corresponding driver is
 * looked for in the list of curenlty loaded drivers. If a matching one has been
 * found, it is requested to create the appropriate device.
 *
 * @see \ref kernel_device_driver
 *
 * @{
 */

#pragma once

#include <kernel/error.h>
#include <kernel/types.h>

#include <libalgo/linked_list.h>

#include <stddef.h>

typedef struct device_driver driver_t;

/** @struct device
 *  @brief Represents a device inside the kernel
 */
typedef struct device {

    node_t this; ///< Used to list devices, internal use only

    char *name;       ///< The name of the device
    driver_t *driver; ///< The driver for this device

} device_t;

/** Register a new device.
 *
 * The device must have been allocated and initialized first by its
 * corresponding driver.
 */
error_t device_register(device_t *);

/** @} */
