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
static inline void pmm_bitmap_set(paddr_t pf, u8 availability)
{
    bitmap_assign(g_pmm_free_bitmap, BITMAP_INDEX(pf), availability);
}

/** Return a pageframe's state according to the allocator's bitmap */
static inline int pmm_bitmap_read(paddr_t pageframe)
{
    return bitmap_read(g_pmm_free_bitmap, BITMAP_INDEX(pageframe));
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
    // If bit 6 in the flags uint16_t is set, then the mmap_* fields are valid
    if (!BIT_READ(mbt->flags, 6)) {
        log_err("PMM", "Multiboot structure does not support memory map.");
        return false;
    }

    // Mark all pages as UNAVAILABLE
    memset(g_pmm_free_bitmap, PMM_UNAVAILABLE, sizeof(g_pmm_free_bitmap));

    log_info("PMM", "Looking for available pageframes");

    // Count the number of availabe pageframes
    u32 available_pageframes = 0;

    multiboot_uint32_t i;
    for (i = 0; i < mbt->mmap_length; i += sizeof(multiboot_memory_map_t)) {
        multiboot_memory_map_t *ram =
            (multiboot_memory_map_t *)(mbt->mmap_addr + i);

        log_dbg("PMM",
                "Start Addr: " LOG_FMT_64 " | Length: " LOG_FMT_64
                " | Size: " LOG_FMT_32 " | Type: %s",
                ram->addr, ram->len, ram->size,
                (ram->type == 0x1) ? "AVAILABLE" : "UNAVAILABLE");

        // If the RAM range is marked as available, we can use the pages it
        // contains for memory allocation.
        if (ram->type == MULTIBOOT_MEMORY_AVAILABLE) {
            for (paddr_t addr = ram->addr; addr < ram->addr + ram->len;
                 addr += PAGE_SIZE) {

                // We still need to check whether the pages are located inside
                // our kernel's code, or we would be succeptible to overwrite
                // its code.
                if (IN_RANGE(KERNEL_HIGHER_HALF_VIRTUAL(addr),
                             KERNEL_CODE_START, KERNEL_CODE_END))
                    continue;
                pmm_bitmap_set(addr, PMM_AVAILABLE);
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

static void pmm_allocator_allocate(pmm_frame_allocator *allocator,
                                   paddr_t address)
{
    pmm_bitmap_set(address, PMM_UNAVAILABLE);

    if (address != allocator->first_available)
        return;

    // Compute the next available pageframe
    allocator->first_available = address;
    while (allocator->first_available <= 0xFFFFFFFF &&
           pmm_bitmap_read(allocator->first_available) != PMM_AVAILABLE)
        allocator->first_available += PAGE_SIZE;
}

/** @} */

bool pmm_init(struct multiboot_info *mbt)
{
    log_info("PMM", "Initializing pageframe allocator");

    if (!pmm_initialize_bitmap(mbt)) {
        return false;
    }

    FOREACH_MULTIBOOT_MODULE (module, mbt) {
        for (paddr_t page = module->mod_start; page <= module->mod_end;
             page += PAGE_SIZE) {
            pmm_allocator_allocate(&g_pmm_allocator, page);
        }
    }

    return true;
}

paddr_t pmm_allocate(int flags)
{
    pmm_frame_allocator *allocator = &g_pmm_allocator;

    UNUSED(flags);

    if (!allocator->initialized) {
        log_err("PMM", "Trying to allocate using an uninitialized allocator");
        return PMM_INVALID_PAGEFRAME; // EINVAL
    }

    u64 address = allocator->first_available;
    if (address > 0xFFFFFFFF)
        return PMM_INVALID_PAGEFRAME; // ENOMEM

    pmm_allocator_allocate(allocator, address);

    return (u32)address;
}

void pmm_free(paddr_t pageframe)
{
    if (IN_RANGE(KERNEL_HIGHER_HALF_VIRTUAL(pageframe), KERNEL_CODE_END,
                 KERNEL_CODE_START)) {
        log_err("PMM", "Trying to free kernel code pages: " LOG_FMT_32,
                pageframe);
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

    pmm_bitmap_set(pageframe, PMM_AVAILABLE);

    if (pageframe < allocator->first_available)
        allocator->first_available = pageframe;
}
