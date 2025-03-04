#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

char *getenv(const char *);

void *malloc(size_t);
void *calloc(size_t, size_t);
void free(void *);

int atoi(const char *);

void abort(void);

int abs(int);

int atexit(void (*)(void));

#endif /* _STDLIB_H */
