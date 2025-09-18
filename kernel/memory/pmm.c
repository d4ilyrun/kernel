#define PFX_DOMAIN "pmm"

#include <kernel/cpu.h>
#include <kernel/devices/block.h>
#include <kernel/interrupts.h>
#include <kernel/logger.h>
#include <kernel/pmm.h>
#include <kernel/types.h>
#include <kernel/vfs.h>

#include <utils/bits.h>
#include <utils/compiler.h>
#include <utils/macro.h>
#include <utils/math.h>

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
 * @brief The static array of all existing pages
 *
 * @note The arrays's size is hardcoded to be able to fit each and every
 *       pageframes (even though only part of them will be available at
 *       runtime).
 */
struct page pmm_pageframes[TOTAL_PAGEFRAMES_COUNT];

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
    paddr_t start;           ///< Start of the allocator's physical range
    paddr_t end;             ///< End of the allocator's physical range
    bool initialized;        ///< Whether this allocator has been initialized
} pmm_frame_allocator;

/** Allocator for 'classical' pageframes */
static pmm_frame_allocator g_pmm_allocator = {
    .start = PHYSICAL_MEMORY_START,
    .end = PHYSICAL_MEMORY_END,
    .first_available = PMM_INVALID_PAGEFRAME,
    .initialized = false,
};

/* TODO: Take in the PFN as parameter directly */
static inline void pmm_set_availability(paddr_t pageframe, bool available)
{
    struct page *page = &pmm_pageframes[TO_PFN(pageframe)];

    if (available) {
        page->flags |= PAGE_AVAILABLE;
        page->refcount = 0;
    } else {
        page->flags &= ~PAGE_AVAILABLE;
        page->refcount = 1;
    }
}

static inline bool pmm_is_available(paddr_t pageframe)
{
    struct page *page = &pmm_pageframes[TO_PFN(pageframe)];
    return boolean(page->flags & PAGE_AVAILABLE);
}

static inline bool
pmm_is_last_pageframe(pmm_frame_allocator *allocator, paddr_t pageframe)
{
    return pageframe == (allocator->end - PAGE_SIZE);
}

/**
 * @brief Initialize the static array of page structs
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
static bool pmm_initialize_pages(struct multiboot_info *mbt)
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
        log_err("Could not find memory map");
        return false;
    }

    // Mark all pages as unavailable.
    // The page's addresss will be filled in if it is available.
    memset(pmm_pageframes, 0, sizeof(pmm_pageframes));

    log_info("Memory ranges:");

    // Count the number of availabe pageframes
    u32 available_pageframes = 0;

    for (entry = mmap->entries; (void *)entry < multiboot_tag_end(mmap);
         entry = (void *)entry + mmap->entry_size) {

        if (entry->type == MULTIBOOT_MEMORY_AVAILABLE ||
            entry->type == MULTIBOOT_MEMORY_RESERVED) {
            log_info("  %s [" FMT32 "-" FMT32 "]",
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

                if (!IN_RANGE(addr, g_pmm_allocator.start, g_pmm_allocator.end))
                    continue;

                pmm_set_availability(addr, true);
                available_pageframes += 1;

                pmm_frame_allocator *allocator = &g_pmm_allocator;
                if (allocator->first_available == PMM_INVALID_PAGEFRAME) {
                    allocator->first_available = addr;
                    allocator->initialized = true;
                }
            }
        }
    }

    log_info("Found %d available pageframes (~%dMiB)", available_pageframes,
             (available_pageframes * PAGE_SIZE) / (2 << 19));
    log_dbg("Total pageframes: %lld", TOTAL_PAGEFRAMES_COUNT);
    log_dbg("First available pageframe: " FMT32,
            g_pmm_allocator.first_available);

    return true;
}

static void pmm_allocator_allocate_at(pmm_frame_allocator *allocator,
                                      paddr_t address, size_t size)
{
    for (size_t off = 0; off < size; off += PAGE_SIZE)
        pmm_set_availability(address + off, false);

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

static void
pmm_allocator_free_at(pmm_frame_allocator *allocator, paddr_t pageframe)
{
    if (allocator->first_available == PMM_INVALID_PAGEFRAME ||
        pageframe < allocator->first_available)
        allocator->first_available = pageframe;
}

/** @} */

struct page *page_get(struct page *page)
{
    page->refcount += 1;
    return page;
}

void page_put(struct page *page)
{
    if (page->refcount == 0)
        return;

    page->refcount -= 1;

    if (page->flags & PAGE_VNODE) {
        WARN_ON_MSG(page->refcount == 0,
                    "file mapped page refcount is 0: vnode(dev=%s)",
                    block_device_name(page->vnode->fs->blkdev));
        /*
         * The page cache itself holds the last reference to the page, which
         * will be released by vnode_put_page() -> page_cache_put(). We do not
         * want to release the cached entry during this last release, so we
         * clear the VNODE flag early.
         */
        if (page->refcount == 1)
            page->flags &= ~PAGE_VNODE;
        vfs_vnode_put_page(page->vnode, page);
    } else {
        if (page->refcount == 0)
            pmm_allocator_free_at(&g_pmm_allocator, page_address(page));
    }
}

bool pmm_init(struct multiboot_info *mbt)
{
    log_info("Initializing pageframe allocator");

    if (!pmm_initialize_pages(mbt)) {
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

paddr_t pmm_allocate_pages(size_t size)
{
    pmm_frame_allocator *allocator = &g_pmm_allocator;
    bool found = false;
    paddr_t address;

    if (!allocator->initialized) {
        log_err("Trying to allocate using an uninitialized allocator");
        return PMM_INVALID_PAGEFRAME;
    }

    if (allocator->first_available == PMM_INVALID_PAGEFRAME) {
        log_err("No available pageframe left");
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
        log_err("Trying to free kernel code pages: [" FMT32 "-" FMT32 "]",
                pageframe, pageframe + (native_t)size);
        return;
    }

    if (pageframe % PAGE_SIZE) {
        log_err("free: pageframe physical address is not aligned on "
                "pagesize: " FMT32,
                pageframe);
        return;
    }

    /* pages are released when their refcount reaches 0 */
    for (size_t off = 0; off < size; off += PAGE_SIZE)
        page_put(address_to_page(pageframe + off));
}
