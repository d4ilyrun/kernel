/**
 * @defgroup kernel_devices_acpi ACPI
 * @ingroup kernel_device
 *
 * # ACPI
 *
 * ACPI (Advanced Configuration and Power Interface) is a Power Management and
 * configuration standard for the PC, developed by Intel, Microsoft and Toshiba.
 * ACPI allows the operating system to control the amount of power each device
 * is given.
 *
 * More generally, in this kernel, we use ACPI to detect which hardware devices
 * are connected before starting up the appropriate driver.
 *
 * ## uACPI
 *
 * There are 2 main parts to ACPI:
 * * Information tables provided by the BIOS
 * * A runtime environment (including an OOP language)
 *
 * Interacting with ACPI tables would require a huge work of reading and parsing
 * those, which is a project in and of itself. For convenience, we use an
 * external library, \c uACPI, which performs all the heavy lifting for us.
 *
 * For more information about it, please refer to their README.
 *
 * @see https://github.com/UltraOS/uACPI
 *
 * @{
 */

#pragma once

#include <kernel/devices/driver.h>
#include <kernel/error.h>

#include <multiboot.h>

/** Per-bus driver struct for ACPI drivers
 *  @see device_driver
 */
struct acpi_driver {
    struct device_driver driver;
    ///< The ACPI ID of the device compatible with this driver
    const char *const compatible;
};

#define ACPI_ID_MAX_LEN 8

/** Per-bus device struct for ACPI devices
 *  @see device
 */
struct acpi_device {
    device_t device;
    char id[ACPI_ID_MAX_LEN]; ///< The device's ACPI id
};

/** Register an ACPI device driver */
void acpi_driver_register(struct acpi_driver *);

#define ACPI_DECLARE_DRIVER(_name, _driver) \
    DECLARE_DRIVER(_name, _driver, acpi_driver_register)

/** Initialize the ACPI environment
 *  @param mbt The multiboot info structure passed by the bootloader
 */
error_t acpi_init(struct multiboot_info *mbt);

/** @} */
