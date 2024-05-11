/**
 * @file kernel/syscalls.h
 *
 * @defgroup kernel_syscalls Syscalls
 * @ingroup kernel
 *
 * # Syscalls
 *
 * This module contains the definition for our kernel's syscalls.
 *
 * I'll try to stick to something similar to POSIX syscalls as much as possible,
 * as it is what I've already become accustomed to, and that this should make
 * eventually porting programs less of a hassle.
 *
 * Some core POSIX concepts are still missing, so some syscalls might be missing
 * crucial arguments (namely file descriptors). This will surely change in the
 * future once we implement the necessary aspects of the kernel.
 *
 * So, as always, this API is subject to change along with the kernel.
 *
 * @{
 */
#ifndef KERNEL_SYSCALLS_H
#define KERNEL_SYSCALLS_H

#include <stddef.h>
#include <stdint.h>

/** Send data to the serial port.
 *
 * @param buf Buffer containing the data
 * @param count the number of bytes from the buffer to send
 *
 * @return the number of bytes written, -1 on error.
 */
int write(const char *buf, size_t count);

/** Read data from the serial port.
 *
 * @param buf Buffer containing the data read
 * @param count the number of bytes to be read
 *
 * @warning The buffer mus tbe large enough to recieve \c count data
 *
 * @return the number of bytes read, -1 on error.
 */
int read(void *buf, size_t count);

/**
 * Get the number of time in ms elapsed since the machine started.
 *
 * @return The time in miliseconds
 */
uint64_t gettime(void);

/**
 * Create a new mapping in the virtual address space of the calling process.
 *
 * @param addr The starting address of the new mapping
 * @param length The length of the mapping (must be greater than 0)
 * @param prot Protection flags for the mapping.
 *             Must be a combination of @ref mmu_prot
 * @param flags Feature flags for the mapping (mapping, sharing, etc ...).
 *              Must be a combination of @ref vma_flags
 */
void *mmap(void *addr, size_t length, int prot, int flags);

/**
 * Delete a mapping for the specified address range
 *
 * @param addr The starting address of the range
 * @param length The length of the range
 *
 * The starting address MUST be page aligned (EINVAL).
 *
 * @return A value of type @ref error_t
 *
 * @info Every page that the range is inside of will be unmaped, even if it's
 *       only for one byte. Beware of overflows and mind the alignment!
 */
int munmap(void *addr, size_t length);

#endif /* KERNEL_SYSCALLS_H */
