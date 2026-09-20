#include <stdlib.h>

void *memmove(void *dst_ptr, const void *src_ptr, size_t count)
{
    char *dst = dst_ptr;
    const char *src = src_ptr;

    if (src < dst) {
        src += count;
        dst += count;

        while (count--)
            *--dst = *--src;
    } else {
        while (count--)
            *dst++ = *src++;
    }

    return dst;
}
