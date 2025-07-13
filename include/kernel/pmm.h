/**
 * @file kernel/pmm.h
 *
 * @defgroup PMM Physical Memory Manager
 * @ingroup kernel
 *
 * # Physical Memory Manager
 *
 * The PMM is responsible for allocating and freeing new memory pageframes.
 * These pages are then used by the Virtual Memory Manager (e.g. UNIX's mmap)
 * to return new (or free) mapped virtual addresses to the caller.
 *
 * The PMM should never interact with the virtual address space, this is the
 * responsabillity of the VMM only.
 *
 * ## Design
 *
 * A physical pageframe is represented by a @ref struct page.
 * The PMM keeps track of every existing physical page inside a statically
 * allocated array. This array is indexed using the page's "pageframenumber".
 * A pageframe number (PFN) is simply the page's physical address divided by the
 * architecture's page size.
 *
 * @todo  TODO: Implement a buddy Allocator
 *        The buddy allocator is less memory efficient, but way faster when it
 *        comes to retrieving available pages.
 *
 * @{
 */

#ifndef KERNEL_PMM_H
#define KERNEL_PMM_H

#include <kernel/memory.h>
#include <kernel/types.h>

#include <utils/bits.h>

#include <multiboot.h>
#include <stdbool.h>

/** Error value returned by the PMM in case of errors */
#define PMM_INVALID_PAGEFRAME (0xFFFFFFFFUL)

/**
 * @brief Total number of pageframes
 *
 * This is the **theorical** total number of pageframes available inside the
 * whole address space.
 *
 * **BUT**, not all pageframes are necessarily available (usable for memory
 * allocation). Some are reserved, used for ACPI, plus RAM is not guaranteed to
 * be contiugous.
 *
 * This constant should **ONLY** be used as a compile-time known theoretical
 * reference value (e.g. the physical memory manager's bit map size).
 */
#define TOTAL_PAGEFRAMES_COUNT (ADDRESS_SPACE_SIZE / PAGE_SIZE)

/** Flags used for @ref struct page 'flags' field
 *  @enum page_flags
 */
enum page_flags {
    PAGE_AVAILABLE = BIT(0), ///< This page has not been allocated
    PAGE_COW = BIT(1),       ///< Currently used in a CoW mapping
};

/** Represents a physical pageframe
 *  @struct page
 */
struct page {
    uint8_t flags;    ///< Combination of @ref page_flags
    uint8_t refcount; ///< How many processes reference that page
};

/**
 * @brief The array of all existing pageframes
 *
 * @note The arrays's size is hardcoded to be able to fit each and every
 *       pageframes (even though only part of them will be available at
 *       runtime).
 */
extern struct page pmm_pageframes[TOTAL_PAGEFRAMES_COUNT];

/** Convert pageframe address to page frame number */
#define TO_PFN(_pageframe) (((native_t)(_pageframe)) >> PAGE_SHIFT)
/** Convert pageframe number to pageframe address */
#define FROM_PFN(_pageframe) ((_pageframe) << PAGE_SHIFT)

/** @return A page's physical address */
static inline paddr_t page_address(const struct page *page)
{
    return FROM_PFN((page - pmm_pageframes) / sizeof(*page));
}

/** @return The page struct corresponding to a pageframe number */
static inline struct page *pfn_to_page(unsigned int pfn)
{
    return &pmm_pageframes[pfn];
}

/** @return The page struct corresponding to a physical address's pageframe */
static inline struct page *address_to_page(paddr_t addr)
{
    return pfn_to_page(TO_PFN(addr));
}

/** Increase the page's refcount. */
struct page *page_get(struct page *page);

/** Decrease the page's refcount. If it reaches 0, the page is released. */
void page_put(struct page *page);

/** @return whether the page is part of a CoW mapping. */
static inline bool page_is_cow(struct page *page)
{
    return page->flags & PAGE_COW;
}

/**
 * @brief Initialize the Physical Memory Mapper
 *
 * * Identify all the available page frames
 * * Locate the first page frame
 *
 * The list of available page frames is retrieved from the memory map
 * passed by our multiboot compliant bootloader.
 *
 * @param multiboot_info The information struct passed by the
 * multiboot-compliant bootloader to our entry function
 *
 * @warning This function should be called only once when starting the kernel.
 * Otherwise it will overwrite the content of the underlying structures.
 */
bool pmm_init(struct multiboot_info *);

/**
 * @brief Allocate previously unused pageframes
 *
 * @info The returned pageframes are guaranteed to be contiguous
 *
 * @param size The size of the area to allocate (must me a multiple of
 * PAGE_SIZE)
 * @param flags Allocation flags
 *
 * @return The pageframe range's **physical** address, PMM_INVALID_PAGEFRAME on
 * error
 */
paddr_t pmm_allocate_pages(size_t size);

/** Release previously allocated contiguous pageframes */
void pmm_free_pages(paddr_t pageframe, size_t size);

/**
 * @brief Allocate a previously unused pageframe
 * @return The pageframe's **physical** address, PMM_INVALID_PAGEFRAME on error
 */
#define pmm_allocate() pmm_allocate_pages(PAGE_SIZE)

#define pmm_free(pageframe) pmm_free_pages(pageframe, PAGE_SIZE)

#endif /* KERNEL_PMM_H */

/** @} */
