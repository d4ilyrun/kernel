#include <string.h>

/* Return the length of the null-terminated string STR. */
size_t strlen(const char *str)
{
    size_t len = 0;

    while (*str++)
        len += 1;

    return len;
}
