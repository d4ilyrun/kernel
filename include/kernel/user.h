/**
 * @file kernel/user.h
 * @brief Userland control mechanisms
 *
 * * User/kernel buffer management
 * * User credentials
 *
 */

#ifndef KERNEL_USER_H
#define KERNEL_USER_H

#include <kernel/types.h>
#include <kernel/error.h>

typedef struct user_buffer {
    void *buffer;
} user_buffer_t;

typedef struct kernel_buffer {
    void *buffer;
} kernel_buffer_t;

#define make_user_buffer(_p) ((struct user_buffer){ .buffer = (_p) })
#define make_kernel_buffer(_p) ((struct kernel_buffer){ .buffer = (_p) })

kernel_buffer_t user_copy_string(user_buffer_t from, size_t max_size);

#endif /* KERNEL_USER_H */
