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

char *strlcpy(char *dst, const char *src, size_t n)
{
    if (n == 0)
        return dst;

    strncpy(dst, src, n - 1);
    dst[n - 1] = '\0';

    return dst;
}

int strcmp(const char *s1, const char *s2)
{
    for (; *s1 && *s2; s1++, s2++) {
        if (*s1 == *s2)
            continue;
        return *s1 - *s2;
    }

    if (*s1)
        return 1;
    if (*s2)
        return -1;

    return 0;
}

int strncmp(const char *s1, const char *s2, size_t count)
{
    for (; count > 0 && *s1 && *s2; s1++, s2++, count--) {
        if (*s1 == *s2)
            continue;
        return *s1 - *s2;
    }

    if (count > 0) {
        if (*s1)
            return 1;
        if (*s2)
            return -1;
    }

    return 0;
}

int memcmp(const void *d1, const void *d2, size_t count)
{
    const char *s1 = d1;
    const char *s2 = d2;

    for (; count > 0; s1++, s2++, count--) {
        if (*s1 == *s2)
            continue;
        return *s1 - *s2;
    }

    if (count > 0) {
        if (*s1)
            return 1;
        if (*s2)
            return -1;
    }

    return 0;
}
