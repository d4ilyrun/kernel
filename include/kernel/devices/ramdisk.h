/**
 * Ramdisk devices are used to use the main memory as a block device.
 *
 * This is used during the initialization phase to mount the initial filesystem.
 * A ramdisk file is read-only from userland, but the created block device can
 * be read and written from.
 *
 * This is directly inspired from Linux's own ramdisk device.
 */

#ifndef KERNEL_DEVICES_RAMDISK_H
#define KERNEL_DEVICES_RAMDISK_H

#include <kernel/error.h>
#include <kernel/types.h>

/** Create a ramdisk device.
 */
struct device *ramdisk_create(const char *name, paddr_t start, size_t size);

#endif /* KERNEL_DEVICES_RAMDISK_H */
