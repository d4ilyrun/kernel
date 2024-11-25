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
 *
 * @brief Represents a device inside the kernel
 *
 * This structure is tipically embedded in a more specific "per-bus"
 * device-structure. It contains the basic information shared across all types
 * of device, necessary for enumeration, synchronization, etc.
 *
 * The bulk of the interactions should be performed through the device's bus's
 * API (e.g. there could exist a `pci_read(struct pci_device *dev)` function to
 * read from a pci device).
 *
 * @see device_driver for en example of this concept applied to drivers
 */
typedef struct device {

    node_t this; ///< Used to list devices, internal use only

    const char *name; ///< The name of the device
    driver_t *driver; ///< The driver for this device

} device_t;

/** Register a new device.
 *
 * The device must have been allocated and initialized first by its
 * corresponding driver.
 */
error_t device_register(device_t *);

/** Generate generic functions used to read/write a device's regsiters
 *
 * The functions are named: prefix_[read,write][b,w,l]
 *
 * @param _pfx The device's functions' prefix
 * @param _dev_type The device's type
 * @param _reg_field The name of the fields pointing to the device's registers
 * @param _off_type The type used for the offset variable
 *
 * @info \c _off_type must be indirectly castable into an integer
 */
#define generate_device_rw_functions(_pfx, _dev_type, _reg_field, _off_type)   \
    __device_read(u8, b, _pfx, _dev_type, _reg_field, _off_type)               \
        __device_write(u8, b, _pfx, _dev_type, _reg_field, _off_type)          \
            __device_read(u16, w, _pfx, _dev_type, _reg_field, _off_type)      \
                __device_write(u16, w, _pfx, _dev_type, _reg_field, _off_type) \
                    __device_read(u32, l, _pfx, _dev_type, _reg_field,         \
                                  _off_type)                                   \
                        __device_write(u32, l, _pfx, _dev_type, _reg_field,    \
                                       _off_type)

#define __device_read(_type, _type_pfx, _pfx, _device_type, _device_reg_field, \
                      _offset_type)                                            \
    static MAYBE_UNUSED inline _type _pfx##_read##_type_pfx(                   \
        _device_type *device, _offset_type offset)                             \
    {                                                                          \
        return *(_type *)(device->_device_reg_field + offset);                 \
    }

#define __device_write(_type, _type_pfx, _pfx, _device_type,  \
                       _device_reg_field, _offset_type)       \
    static MAYBE_UNUSED inline void _pfx##_write##_type_pfx(  \
        _device_type *device, _offset_type offset, _type val) \
    {                                                         \
        *(_type *)(device->_device_reg_field + offset) = val; \
    }

/** @} */
