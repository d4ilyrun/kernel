#include <kernel/user.h>

#include <string.h>

kernel_buffer_t user_copy_string(user_buffer_t from, size_t max_size)
{
    char *buffer;
    size_t size;

    size = strnlen(from.buffer, max_size);
    if (size == max_size) {
    }
}
