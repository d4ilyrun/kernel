/*
 * System-specific definitions for liballoc
 */

#include <sys/mman.h>

#include "liballoc.h"

int liballoc_lock(void)
{
    return 0;
}

int liballoc_unlock(void)
{
    return 0;
}

void *liballoc_alloc(size_t size)
{
    void *addr;

    addr = mmap(NULL, size, PROT_READ | PROT_WRITE,
                MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
    if (addr == MAP_FAILED)
        return NULL;

    return addr;
}

int liballoc_free(void *addr, size_t size)
{
    if (!addr)
        return 0;

    return munmap(addr, size);
}
