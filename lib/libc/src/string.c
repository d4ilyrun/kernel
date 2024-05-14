#include <string.h>

/* Return the length of the null-terminated string STR. */
size_t strlen(const char *str)
{
    size_t len = 0;

    while (*str++)
        len += 1;

    return len;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        dst[i] = src[i];
        if (dst[i] == '\0')
            break;
    }

    return dst;
}
