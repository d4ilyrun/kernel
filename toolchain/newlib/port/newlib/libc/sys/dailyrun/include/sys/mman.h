#ifndef _SYS_MMAN_H
#define _SYS_MMAN_H

#include <sys/types.h>

#define MAP_ANONYMOUS 0x1
#define MAP_ANON      MAP_ANONYMOUS
#define MAP_FIXED     0x2
#define MAP_PRIVATE   0x4

#define PROT_NONE     0x0
#define PROT_EXEC     0x1
#define PROT_READ     0x2
#define PROT_WRITE    0x4

#ifdef KERNEL
#define PROT_KERNEL 0x8
#define PROT_MASK   0xf
#else
#define PROT_MASK   0x7
#endif

/* returned when mmap() fails */
#define MAP_FAILED    ((void *)-1)

#ifndef KERNEL
void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off);
void *munmap(void *addr, size_t len);
int shm_open(const char *name, int oflag, mode_t mode);
int shm_unlink(const char *name);
#endif

#endif /* _SYS_MMAN_H */
