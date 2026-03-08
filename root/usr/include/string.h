#ifndef _STRING_H
#define _STRING_H

#include <stddef.h>

size_t strlen(const char *s);

char *strncpy(char *dst, const char *src, size_t);
size_t strlcpy(char *dst, const char *src, size_t);

void *memset(void *s, int c, size_t n);
void *memcpy(void *restrict dest, const void *restrict src, size_t n);
int memcmp(const void *, const void *, size_t);

int strcmp(const char *, const char *);
int strncmp(const char *, const char *, size_t);

char *strcat(char *dst, const char *src);

#endif /* _STRING_H */
