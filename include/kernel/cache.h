#ifndef KERNEL_CACHE_H
#define KERNEL_CACHE_H

#include <kernel/types.h>

void cache_flush(paddr_t addr);
void cache_flush_range(paddr_t addr, size_t range_size);

#endif /* KERNEL_CACHE_H */
