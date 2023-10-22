#ifndef KERNEL_SYSCALLS_H
#define KERNEL_SYSCALLS_H

#include <stddef.h>

/** Send data to the serial port
 *
 * @param buf Buffer containing the data
 * @param count the number of bytes from the buffer to send
 */
int write(const char* buf, size_t count);

#endif /* KERNEL_SYSCALLS_H */
