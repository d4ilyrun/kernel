#include <kernel/i686/interrupts.h> // FIXME: Automatically import this file
#include <kernel/interrupts.h>
#include <kernel/logger.h>
#include <kernel/pmm.h>

#include <assert.h>
#include <multiboot.h>
#include <stddef.h>
#include <string.h>
#include <utils/align.h>
#include <utils/compiler.h>
#include <utils/cpu_ops.h>
#include <utils/macro.h>
#include <utils/types.h>

/// @brief The Physical Memory Allocator
///
/// This allocator is responsible for keeping track of available
/// framepages, returning them for memory allocations and free unused ones.
///
/// For simplicity's sake, we use a bitmap allocator.
/// It keeps track of the available pageframe's index inside a static array.
///
/// @TODO: Implement a buddy Allocator
///        The buddy allocator is less memory efficient, but way faster when it
///        comes to retrieving available pages.
///
/// @info The bitmap's size is hardcoded to be able to fit each and every
/// pageframes (even though only part of them will be available at runtime).
static u32 g_pmm_free_bitmap[TOTAL_PAGEFRAMES_COUNT / (8 * sizeof(u32))];

typedef struct {
    u32 first_available; // Address of the first available pageframe
    u32 start;
    u32 end;
    bool initialized;
} pmm_frame_allocator;

static pmm_frame_allocator g_pmm_user_allocator = {
    .start = 0,
    .end = KERNEL_CODE_START,
    .first_available = PMM_INVALID_PAGEFRAME,
    .initialized = false,
};

static pmm_frame_allocator g_pmm_kernel_allocator = {
    .start = KERNEL_CODE_START,
    .end = ADDRESS_SPACE_END,
    .first_available = PMM_INVALID_PAGEFRAME,
    .initialized = false,
};

// Wether a pageframe is marked available or not inside the allocator's bitmap
#define PMM_AVAILABLE (1)
#define PMM_UNAVAILABLE (0)

#define BITMAP_INDEX(address) ((address / PAGE_SIZE) / 32)

/// Mark a pageframe as PMM_AVAILABLE or PMM_UNAVAILABLE
static inline void pmm_bitmap_set(u32 page, u8 availability)
{
    u32 value = g_pmm_free_bitmap[BITMAP_INDEX(page)];
    if (availability == PMM_AVAILABLE)
        g_pmm_free_bitmap[BITMAP_INDEX(page)] = BIT_SET(value, page % 32);
    else
        g_pmm_free_bitmap[BITMAP_INDEX(page)] = BIT_MASK(value, page % 32);
}

/// @return a pageframe's state according to the allocator's bitmap
static inline int pmm_bitmap_read(u32 page)
{
    return BIT(g_pmm_free_bitmap[BITMAP_INDEX(page)], page % 32);
}

static bool pmm_initialize_bitmap(struct multiboot_info *mbt)
{
    // If bit 6 in the flags uint16_t is set, then the mmap_* fields are valid
    if (!BIT(mbt->flags, 6)) {
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
            for (u32 addr = ram->addr; addr < ram->addr + ram->len;
                 addr += PAGE_SIZE) {

                // We still need to check whether the pages are located inside
                // our kernel's code, or we would be succeptible to overwrite
                // its code.
                if (IN_RANGE(KERNEL_HIGHER_HALF_VIRTUAL(addr),
                             KERNEL_CODE_START, KERNEL_CODE_END))
                    continue;
                pmm_bitmap_set(addr, PMM_AVAILABLE);
                available_pageframes += 1;

                pmm_frame_allocator *allocator =
                    (KERNEL_HIGHER_HALF_VIRTUAL(addr) >= KERNEL_CODE_START)
                        ? &g_pmm_kernel_allocator
                        : &g_pmm_user_allocator;

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
            g_pmm_user_allocator.first_available);

    return true;
}

// TODO: This handler should go inside the VMM's code once implemented.
//       We only defined it here for now for learning/debugging purposes
//       after activating paging.

/// Structure of the page fault's error code
/// @link https://wiki.osdev.org/Exceptions#Page_Fault
typedef struct PACKED {
    u8 present : 1;
    u8 write : 1;
    u8 user : 1;
    u8 reserved_write : 1;
    u8 fetch : 1;
    u8 protection_key : 1;
    u8 ss : 1;
    u16 _unused1 : 8;
    u8 sgx : 1;
    u16 _unused2 : 15;
} page_fault_error;

static DEFINE_INTERRUPT_HANDLER(page_fault)
{
    log_warn("interrupt", "Interrupt recieved: Page fault");
    page_fault_error error = *(page_fault_error *)frame.error;

    log_dbg("[PF] source", "%s access on a %s page %s",
            error.write ? "write" : "read",
            error.present ? "protected" : "non-present",
            error.user ? "whie in user-mode" : "");

    // The CR2 register holds the virtual address which caused the Page Fault
    u32 faulty_address = read_cr2();

    log_dbg("[PF] error", LOG_FMT_32, frame.error);
    log_dbg("[PF] address", LOG_FMT_32, faulty_address);
}

bool pmm_init(struct multiboot_info *mbt)
{
    log_info("PMM", "Initializing pageframe allocator");

    if (!pmm_initialize_bitmap(mbt)) {
        return false;
    }

    interrupts_set_handler(PAGE_FAULT, INTERRUPT_HANDLER(page_fault));

    return true;
}

u32 pmm_allocate(int flags)
{
    pmm_frame_allocator *allocator = BIT(flags, PMM_MAP_KERNEL_BIT)
                                       ? &g_pmm_kernel_allocator
                                       : &g_pmm_user_allocator;

    if (!allocator->initialized) {
        log_err("PMM", "Trying to allocate using an uninitialized allocator");
        return PMM_INVALID_PAGEFRAME; // EINVAL
    }

    u64 address = allocator->first_available;
    if (address > 0xFFFFFFFF)
        return PMM_INVALID_PAGEFRAME; // ENOMEM

    pmm_bitmap_set(address, PMM_UNAVAILABLE);

    // Compute the next available pageframe
    allocator->first_available = address;
    while (allocator->first_available <= 0xFFFFFFFF &&
           pmm_bitmap_read(allocator->first_available) != PMM_AVAILABLE)
        allocator->first_available += PAGE_SIZE;

    return (u32)address;
}

void pmm_free(u32 pageframe)
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

    pmm_frame_allocator *allocator =
        (KERNEL_HIGHER_HALF_VIRTUAL(pageframe) >= KERNEL_CODE_START)
            ? &g_pmm_kernel_allocator
            : &g_pmm_user_allocator;

    pmm_bitmap_set(pageframe, PMM_AVAILABLE);

    if (pageframe < allocator->first_available)
        allocator->first_available = pageframe;
}
