#ifndef KERNEL_SYSCALLS_H
#define KERNEL_SYSCALLS_H

#include <stddef.h>
#include <stdint.h>

/** Send data to the serial port
 *
 * @param buf Buffer containing the data
 * @param count the number of bytes from the buffer to send
 *
 * @returns the number of bytes written, -1 on error.
 */
int write(const char *buf, size_t count);

/** Read data from the serial port
 *
 * @param buf Buffer containing the data read
 * @param count the number of bytes to be read
 *
 * @warning The buffer mus tbe large enough to recieve \c count data
 *
 * @returns the number of bytes read, -1 on error.
 */
int read(void *buf, size_t count);

/**
 * Get the number of time in ms elapsed since the machine started.
 *
 * @returns The time in miliseconds
 */
uint64_t gettime(void);

#endif /* KERNEL_SYSCALLS_H */
