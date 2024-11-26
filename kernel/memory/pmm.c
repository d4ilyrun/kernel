#include <kernel/cpu.h>
#include <kernel/interrupts.h>
#include <kernel/logger.h>
#include <kernel/pmm.h>
#include <kernel/types.h>

#include <libalgo/bitmap.h>
#include <utils/bits.h>
#include <utils/compiler.h>
#include <utils/macro.h>
#include <utils/math.h>

#include <assert.h>
#include <multiboot.h>
#include <stddef.h>
#include <string.h>

/**
 * @defgroup pmm_internals Internal structures and definitions
 * @ingroup PMM
 *
 * Internal structures and definitions used by the PMM.
 * No module outside of the PMM should access these.
 *
 * @{
 */

/**
 * @brief The Physical Memory Allocator
 *
 * This allocator is responsible for keeping track of available
 * framepages, returning them for memory allocations and free unused ones.
 *
 * For simplicity's sake, we use a bitmap allocator.
 * It keeps track of the available pageframe's index inside a static array.
 *
 * @todo  TODO: Implement a buddy Allocator
 *        The buddy allocator is less memory efficient, but way faster when it
 *        comes to retrieving available pages.
 *
 * @note The bitmap's size is hardcoded to be able to fit each and every
 *       pageframes (even though only part of them will be available at
 *       runtime).
 */
static BITMAP(g_pmm_free_bitmap, TOTAL_PAGEFRAMES_COUNT);

/**
 * @struct pmm_frame_allocator
 * @brief A pageframe allocator
 *
 * As we are using multiple ranges, we need multiple allocators.
 *
 * FIXME: There might be a confusion between vaddr/paddr here
 *        Why would our pmm refer to the kernel's vitrual addresses
 *        to determine its range?
 */
typedef struct {
    paddr_t first_available; ///< Address of the first available pageframe
    vaddr_t start;           ///< Start of the allocator's physical range
    vaddr_t end;             ///< End of the allocator's physical range
    bool initialized;        ///< Whether this allocator has been initialized
} pmm_frame_allocator;

/** Allocator for 'classical' pageframes */
static pmm_frame_allocator g_pmm_allocator = {
    .start = 0,
    .end = ADDRESS_SPACE_END,
    .first_available = PMM_INVALID_PAGEFRAME,
    .initialized = false,
};

// Wether a pageframe is marked available or not inside the allocator's bitmap
#define PMM_AVAILABLE (1)
#define PMM_UNAVAILABLE (0)

#define BITMAP_INDEX(address) (address / PAGE_SIZE)

/** Mark a pageframe as PMM_AVAILABLE or PMM_UNAVAILABLE */
static inline void pmm_set_availability(paddr_t pf, u8 availability)
{
    bitmap_assign(g_pmm_free_bitmap, BITMAP_INDEX(pf), availability);
}

/** Return a pageframe's state according to the allocator's bitmap */
static inline int pmm_is_available(paddr_t pageframe)
{
    return bitmap_read(g_pmm_free_bitmap, BITMAP_INDEX(pageframe));
}

static inline bool
pmm_is_last_pageframe(pmm_frame_allocator *allocator, paddr_t pageframe)
{
    return pageframe == (allocator->end - PAGE_SIZE);
}

/**
 * @brief Initialize the pageframe bitmap
 *
 * Using the bootloader's information, we mark unusable pageframes as being
 * unavailable. The kernel's code is also marked as unavailable, since this
 * distinction is not made by the bootloader.
 *
 * Finally, we initialize the allocator structs, locating their first
 * respective available address.
 *
 * @param mbt The information passed on by the bootloader
 *
 * @return Whether the initialization process succeeded
 */
static bool pmm_initialize_bitmap(struct multiboot_info *mbt)
{
    struct multiboot_tag_mmap *mmap = NULL;
    multiboot_memory_map_t *entry;

    FOREACH_MULTIBOOT_TAG (tag, mbt) {
        if (tag->type == MULTIBOOT_TAG_TYPE_MMAP) {
            mmap = (void *)tag;
            break;
        }
    }

    if (mmap == NULL) {
        log_err("PMM", "Could not find memory map");
        return false;
    }

    // Mark all pages as UNAVAILABLE
    memset(g_pmm_free_bitmap, PMM_UNAVAILABLE, sizeof(g_pmm_free_bitmap));

    log_info("PMM", "Memory ranges:");

    // Count the number of availabe pageframes
    u32 available_pageframes = 0;

    for (entry = mmap->entries; (void *)entry < multiboot_tag_end(mmap);
         entry = (void *)entry + mmap->entry_size) {

        if (entry->type == MULTIBOOT_MEMORY_AVAILABLE ||
            entry->type == MULTIBOOT_MEMORY_RESERVED) {
            log_info("PMM", "  %s [" LOG_FMT_32 "-" LOG_FMT_32 "]",
                     (entry->type == MULTIBOOT_MEMORY_RESERVED) ? "reserved "
                                                                : "available",
                     (uint32_t)entry->addr,
                     (uint32_t)(entry->addr + entry->len));
        }

        // If the RAM range is marked as available, we can use the pages it
        // contains for memory allocation.
        if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
            for (paddr_t addr = entry->addr; addr < entry->addr + entry->len;
                 addr += PAGE_SIZE) {

                // We still need to check whether the pages are located inside
                // our kernel's code, or we would be succeptible to overwrite
                // its code.
                if (IN_RANGE(KERNEL_HIGHER_HALF_VIRTUAL(addr),
                             KERNEL_CODE_START, KERNEL_CODE_END))
                    continue;
                pmm_set_availability(addr, PMM_AVAILABLE);
                available_pageframes += 1;

                pmm_frame_allocator *allocator = &g_pmm_allocator;
                if (allocator->first_available == PMM_INVALID_PAGEFRAME) {
                    allocator->first_available = addr;
                    allocator->initialized = true;
                }
            }
        }
    }

    log_info("PMM", "Found %ld available pageframes (~%ldMiB)",
             available_pageframes,
             (available_pageframes * PAGE_SIZE) / (2 << 19));
    log_dbg("PMM", "Total pageframes: %ld", TOTAL_PAGEFRAMES_COUNT);
    log_dbg("PMM", "First available pageframe: " LOG_FMT_32,
            g_pmm_allocator.first_available);

    return true;
}

static void pmm_allocator_allocate_at(pmm_frame_allocator *allocator,
                                      paddr_t address, size_t size)
{
    for (size_t off = 0; off < size; off += PAGE_SIZE)
        pmm_set_availability(address + off, PMM_UNAVAILABLE);

    // Need to update the first available pageframe
    if (address != allocator->first_available)
        return;

    for (address += size; !pmm_is_last_pageframe(allocator, address);
         address += PAGE_SIZE) {
        if (pmm_is_available(address))
            break;
    }

    if (!pmm_is_available(address))
        allocator->first_available = PMM_INVALID_PAGEFRAME;

    allocator->first_available = address;
}

/** @} */

bool pmm_init(struct multiboot_info *mbt)
{
    log_info("PMM", "Initializing pageframe allocator");

    if (!pmm_initialize_bitmap(mbt)) {
        return false;
    }

    FOREACH_MULTIBOOT_TAG (tag, mbt) {
        if (tag->type != MULTIBOOT_TAG_TYPE_MODULE)
            continue;
        struct multiboot_tag_module *module = (void *)tag;
        pmm_allocator_allocate_at(&g_pmm_allocator, module->mod_start,
                                  module->mod_end - module->mod_start);
    }

    return true;
}

paddr_t pmm_allocate_pages(size_t size, int flags)
{
    pmm_frame_allocator *allocator = &g_pmm_allocator;
    bool found = false;
    paddr_t address;

    UNUSED(flags);

    if (!allocator->initialized) {
        log_err("PMM", "Trying to allocate using an uninitialized allocator");
        return PMM_INVALID_PAGEFRAME;
    }

    if (allocator->first_available == PMM_INVALID_PAGEFRAME) {
        log_err("PMM", "No available pageframe left");
        return PMM_INVALID_PAGEFRAME;
    }

    /*
     * Minimum allocatable size is a page.
     * Always allocate more than requested when necessary.
     */
    size = align_up(size, PAGE_SIZE);

    address = allocator->first_available;

    while (!found && address >= allocator->first_available) {
        found = true;
        /* Find first next available */
        while (!pmm_is_available(address))
            address += PAGE_SIZE;
        /* Check if enough pageframes are available starting at this address */
        for (size_t off = 0; off < size; off += PAGE_SIZE) {
            if (!pmm_is_available(address + off)) {
                found = false;
                address += off; /* Continue searching after this */
                break;
            }
        }
    }

    if (!found)
        return PMM_INVALID_PAGEFRAME;

    pmm_allocator_allocate_at(allocator, address, size);

    return address;
}

void pmm_free_pages(paddr_t pageframe, size_t size)
{
    if (RANGES_OVERLAP(KERNEL_HIGHER_HALF_VIRTUAL(pageframe),
                       KERNEL_HIGHER_HALF_VIRTUAL(pageframe) + size,
                       KERNEL_CODE_END, KERNEL_CODE_START)) {
        log_err("PMM",
                "Trying to free kernel code pages: [" LOG_FMT_32 "-" LOG_FMT_32
                "]",
                pageframe, pageframe + size);
        return;
    }

    if (pageframe % PAGE_SIZE) {
        log_err("PMM",
                "free: pageframe physical address is not aligned on "
                "pagesize: " LOG_FMT_32,
                pageframe);
        return;
    }

    pmm_frame_allocator *allocator = &g_pmm_allocator;

    for (size_t off = 0; off < size; off += PAGE_SIZE)
        pmm_set_availability(pageframe + off, PMM_AVAILABLE);

    if (allocator->first_available == PMM_INVALID_PAGEFRAME ||
        pageframe < allocator->first_available)
        allocator->first_available = pageframe;
}
