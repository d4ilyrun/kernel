#include <string.h>

void *memcpy(void *restrict dest, const void *restrict src, size_t n)
{
    char *dst = dest;
    const char *src_char = src;

    for (size_t i = 0; i < n; i++) {
        dst[i] = src_char[i];
    }

    return dest;
}
