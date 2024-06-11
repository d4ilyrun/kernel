#pragma once

/**
 * @defgroup kernel_device Devices
 * @ingroup kernel
 *
 * # Devices
 *
 * @{
 */

#include <kernel/types.h>

#include <stddef.h>

#include "kernel/error.h"

/** @struct device
 *  @brief Represents a device
 *
 * In the current implementation, as everything is stored in RAM, the device
 * struct is simply a wrapper arround what could be considered a contiguous
 * buffer in memory.
 *
 * In the future though, the device structure should become a common interface
 * between devices of different types (e.g. ram and hard disks should share this
 * API)
 */
typedef struct device {
    u32 start;   ///< Start address of the device in memory
    size_t size; ///< Size of the memory area for this device

    /** @struct device_operations
     *  @brief VTable of I/O operations for the device
     */
    struct device_operations {
        error_t (*read)(const struct device *, char *buffer, size_t offset,
                        size_t size);
        error_t (*write)(const struct device *, size_t offset,
                         const char *buffer, size_t size);
    } operations;
} dev_t;

/** Create a new device
 *
 *  @warning This implementation only serves as a place holder for the actual
 *           one in order to ease the migration once it is implemneted. This API
 *           **WILL** change.
 *
 *  @param start The starting address of the device's memory range
 *  @param size The size of teh device's memory range
 */
dev_t *device_new(u32 start, size_t size);

/** @} */
