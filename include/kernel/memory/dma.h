#ifndef KERNEL_MEMORY_DMA_H
#define KERNEL_MEMORY_DMA_H

#include <kernel/types.h>
#include <kernel/error.h>

/**
 * Allocate an addressable memory buffer suitable for DMA operations
 *
 * @param size The size of the buffer
 *
 * @note DMA regions are by design always located inside the kernel's space
 *
 * @return A buffer whose physical pageframes are contiguous
 */
void *kmalloc_dma(size_t size);

/** Free a buffer allocated through @ref kmalloc_dma */
void kfree_dma(void *dma_ptr);

void dma_api_init(void);

#endif /* KERNEL_MEMORY_DMA_H */
