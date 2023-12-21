#include <kernel/pmm.h>

#include <stddef.h>
#include <utils/compiler.h>

#include "utils/macro.h"
#include "utils/types.h"

// Number of entries inside the page directory
#define PMM_PDE_COUNT (1024)

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

void pmm_init(void)
{
    // Mark all PDEs as "absent" (present = 0), and writable
    for (size_t entry = 0; entry < PMM_PDE_COUNT; entry++) {
        kernel_page_directory[entry] = (pmm_pde_entry){.writable = 1};
    }

    // FIXME: Do not set the content of CR3 inside this function
    //
    // We set CR3 to be the kernel's page directory, AT ALL TIMES.
    //
    // This works for now since we only have ONE active tasks. But once
    // we are able to execute multiple tasks at once, we should find another
    // way to set its contentt obe that of the currently running process.
    pmm_enable_paging(kernel_page_directory);
}
