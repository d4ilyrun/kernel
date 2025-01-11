/**
 * @file kernel/console.h
 * @defgroup kernel_console Console
 * @ingroup kernel
 *
 * @{
 */
#ifndef KERNEL_CONSOLE_H
#define KERNEL_CONSOLE_H

#include <kernel/device.h>

/** @brief Representation of the kernel's console
 *
 *  All kernel logs are written to the console.
 */
struct console {
    struct file *out; /** File representation of the console's output */
};

/** @brief Console used during kernel initialization
 *
 * It is necessary to use a more primitive console structure during the kernel
 * initialization process, as some necessary features for the regular console
 * may not be properly setup yet (e.g. memory allocation and vfs traversing).
 *
 * Keep in mind that the callbacks for this console should not use such features
 * and should keep things to the bare minimum (writing, nothing more).
 */
struct early_console {
    void *private;                ///< private data used by the callbacks
    error_t (*init)(void *pdata); ///< Called during initialization
    /** Called to write a buffer to the console */
    error_t (*write)(const char *buffer, size_t size, void *pdata);
};

/** Set the console to use during kernel initialization */
error_t console_early_setup(struct early_console *, void *pdata);

/** Set a device as the regular console's output */
error_t console_open(struct device *device);
/** Write a buffer to the console */
error_t console_write(const char *buf, size_t size);

#endif /* KERNEL_CONSOLE_H */

/** @} */
