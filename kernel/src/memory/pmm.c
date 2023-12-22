#include <kernel/i686/interrupts.h> // FIXME: Automatically import this file
#include <kernel/interrupts.h>
#include <kernel/logger.h>
#include <kernel/pmm.h>

#include <assert.h>
#include <multiboot.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <utils/align.h>
#include <utils/compiler.h>
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
typedef struct {
    u32 free_bitmap[TOTAL_PAGEFRAMES_COUNT / (8 * sizeof(u32))];
    u32 first_available; // Address of the first available pageframe
} pmm_frame_allocator;

static pmm_frame_allocator g_pmm_allocator;

// Wether a pageframe is marked available or not inside the allocator's bitmap
#define PMM_AVAILABLE (1)
#define PMM_UNAVAILABLE (0)

#define BITMAP_INDEX(address) ((address / PAGE_SIZE) / 32)

/// Mark a pageframe as PMM_AVAILABLE or PMM_UNAVAILABLE
static inline void pmm_bitmap_set(u32 page, u8 availability)
{
    u32 value = g_pmm_allocator.free_bitmap[BITMAP_INDEX(page)];
    if (availability == PMM_AVAILABLE)
        g_pmm_allocator.free_bitmap[BITMAP_INDEX(page)] =
            BIT_SET(value, page % 32);
    else
        g_pmm_allocator.free_bitmap[BITMAP_INDEX(page)] =
            BIT_MASK(value, page % 32);
}

/// @return whether a page is marked as PMM_AVAILABLE
static inline bool pmm_bitmap_read(u32 page)
{
    return BIT(g_pmm_allocator.free_bitmap[BITMAP_INDEX(page)], page % 32) ==
           PMM_AVAILABLE;
}

// Number of entries inside the page directory
#define PMM_PDE_COUNT (1024)
// Number of entries inside a single page table
#define PMM_PTE_COUNT (1024)

/// Page Directory entry
/// @see Table 4-5
typedef struct PACKED {
    u8 present : 1;  ///< Wether this entry is present
    u8 writable : 1; ///< Read/Write
    u8 user : 1;     ///< User/Supervisor
    u8 pwt : 1;      ///< Page-level write-through
    u8 pcd : 1;      ///< Page-level cache disabled
    u8 accessed : 1; ///< Whether this entry has been used for translation
    u8 _ignored : 1;
    u8 ps : 1;
    u8 _ignored2 : 4;
    u32 page_table : 20; ///< Physical address of the referenced page table
} pmm_pde_entry;

/// Page Table entry
/// @info we use 32-bit page tables that map 4-KiB pages
/// @see Table 4-6
typedef struct PACKED {
    u8 present : 1;  ///< Wether this entry is present
    u8 writable : 1; ///< Read/Write
    u8 user : 1;     ///< User/Supervisor
    u8 pwt : 1;      ///< Page-level write-through
    u8 pcd : 1;      ///< Page-level cache disabled
    u8 accessed : 1; ///< Whether the software has accessed this page
    u8 dirty : 1;    ///< Whether software has written to this page
    u8 pat : 1;
    u8 global : 1;
    u8 _ignored : 3;
    u32 page_frame : 20; ///< Physical address of the referenced page frame
} pmm_pte_entry;

// The kernel's very own page directory
static __attribute__((__aligned__(PAGE_SIZE)))
pmm_pde_entry kernel_page_directory[PMM_PDE_COUNT];

// TODO: Dynamically allocate page table using frame allocator (1:1)
// The kernel's very own page tables
static __attribute__((__aligned__(PAGE_SIZE)))
pmm_pte_entry kernel_page_tables[PMM_PDE_COUNT][PMM_PTE_COUNT];

/// Enable paging and set the current page page directory inside CR3
static void pmm_enable_paging(pmm_pde_entry *page_directory)
{
    // Set CR3 to point to our page directory
    ASM("movl %0, %%cr3" : : "r"(page_directory));

    // According to 4.3, to activate 32-bit mode paging we must:
    // 1. set CR4.PAE to 0 (de-activate PAE)
    u32 cr4;
    ASM("movl %%cr4, %0" : "=r"(cr4));
    cr4 = BIT_MASK(cr4, 5); // PAE = bit 5
    ASM("movl %0, %%cr4" : : "r"(cr4));
    // 2. set CR0.PG to 1  (activate paging)
    u32 cr0;
    ASM("movl %%cr0, %0" : "=r"(cr0));
    cr0 = BIT_SET(cr0, 31); // PG = bit 32
    ASM("movl %0, %%cr0" : : "r"(cr0));
}

static bool pmm_initialize_bitmap(struct multiboot_info *mbt)
{
    // If bit 6 in the flags uint16_t is set, then the mmap_* fields are valid
    if (!BIT(mbt->flags, 6)) {
        log_err("PMM", "Multiboot structure does not support memory map.");
        return false;
    }

    // Mark all pages as UNAVAILABLE
    memset(g_pmm_allocator.free_bitmap, PMM_UNAVAILABLE,
           sizeof(g_pmm_allocator.free_bitmap));

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
                if (IN_RANGE(addr, KERNEL_CODE_START(), KERNEL_CODE_END()))
                    continue;
                pmm_bitmap_set(addr, PMM_AVAILABLE);
                if (available_pageframes++ == 0)
                    g_pmm_allocator.first_available = addr;
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

/// Map a virtual address to a physical pageframe
/// This includes:
/// - Adding the page frame inside the page table
/// - Linking the page table inside the page directory (if needed)
/// - Making sure that it was not previously allocated
static void pmm_mmap(u32 virtual, u32 pageframe)
{
    u16 pde_index = virtual >> 22;                     // bits 31-22
    u16 pte_index = (virtual >> 12) & ((1 << 10) - 1); // bits 21 - 12

    // TODO: Hard-coded to work with kernel page tables only
    //       This should take the pagedir./pagetables as input later on
    //
    // We also hardcode the pde/pte to be un-accessible when in user mode.
    // This will also cause an issue when reaching userspace later.

    if (!kernel_page_directory[pde_index].present) {
        kernel_page_directory[pde_index].page_table =
            (u32)&kernel_page_tables[pde_index] >> 12;
        kernel_page_directory[pde_index].present = 1;
    }

    if (kernel_page_tables[pde_index][pte_index].present) {
        log_err("PMM",
                "Allocating already allocated virtual address: " LOG_FMT_32,
                virtual);
        return;
    }

    // Link the page table entry (virtual address) to the pageframe
    kernel_page_tables[pde_index][pte_index] = (pmm_pte_entry){
        .present = 1,
        .page_frame = pageframe >> 12,
        // TODO: hard-coded values
        .writable = 1,
        .user = 0,
    };
}

/// Identity map addresses inside a given range.
/// This means that, for each virtual address inside this range, their physical
/// equivalent will be identical to the virtual address.
static void pmm_identity_map(uint32_t start, uint32_t end)
{
    for (; start < end; start += PAGE_SIZE) {
        pmm_mmap(start, start);
    }
}

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
    u32 faulty_address;
    ASM("movl %%cr2, %0" : "=r"(faulty_address));

    log_dbg("[PF] error", LOG_FMT_32, frame.error);
    log_dbg("[PF] address", LOG_FMT_32, faulty_address);
}

void pmm_init(struct multiboot_info *mbt)
{
    log_info("PMM", "Initializing pageframe allocator");

    // Mark all PDEs as "absent" (present = 0), and writable
    for (size_t entry = 0; entry < PMM_PDE_COUNT; entry++) {
        kernel_page_directory[entry] = (pmm_pde_entry){.writable = 1};
    }

    if (!pmm_initialize_bitmap(mbt)) {
        // TODO: propagate error
    }

    // For more simplicity, we identity map the content of our kernel.
    pmm_identity_map(align(KERNEL_CODE_START(), PAGE_SIZE), KERNEL_CODE_END());
    // We also map the first 1M of physical memory, it will be reserved for
    // hardware structs.
    pmm_identity_map(0x0, 0x100000);

    interrupts_set_handler(PAGE_FAULT, INTERRUPT_HANDLER(page_fault));

    // FIXME: Do not set the content of CR3 inside this function
    //
    // We set CR3 to be the kernel's page directory, AT ALL TIMES.
    //
    // This works for now since we only have ONE active tasks. But once
    // we are able to execute multiple tasks at once, we should find another
    // way to set its contentt obe that of the currently running process.
    pmm_enable_paging(kernel_page_directory);
}
