#ifndef _STDIO_H
#define _STDIO_H

#include <stdarg.h>
#include <stddef.h>

#define SEEK_SET 0

typedef struct {
    int unused;
} FILE;

extern FILE *stderr;
extern FILE *stdout;

#define stderr stderr
#define stdout stdout

FILE *fopen(const char *, const char *);
int fclose(FILE *);
int fflush(FILE *);

int fprintf(FILE *, const char *, ...);
int vfprintf(FILE *, const char *, va_list);

size_t fwrite(const void *, size_t, size_t, FILE *);
size_t fread(void *, size_t, size_t, FILE *);

int fseek(FILE *, long, int);
long ftell(FILE *);

void setbuf(FILE *, char *);

#endif /* _STDIO_H */
