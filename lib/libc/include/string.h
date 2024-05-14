#ifndef STRING_H
#define STRING_H

#include <stddef.h>

size_t strlen(const char *s);

char *strncpy(char *dst, const char *src, size_t);

void *memset(void *s, int c, size_t n);

void *memcpy(void *restrict dest, const void *restrict src, size_t n);

#endif /* STRING_H */
