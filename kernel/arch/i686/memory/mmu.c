#include <kernel/logger.h>
#include <kernel/mmu.h>
#include <kernel/pmm.h>

#include <stddef.h>
#include <utils/align.h>
#include <utils/compiler.h>
#include <utils/macro.h>
#include <utils/types.h>

// Number of entries inside the page directory
#define MMU_PDE_COUNT (1024)
// Number of entries inside a single page table
#define MMU_PTE_COUNT (1024)

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
    /// @brief Physical address of the referenced page table.
    /// These are the 12 higher bits of the address (i.e. address / PAGE_SIZE)
    u32 page_table : 20;
} mmu_pde_entry;

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
    /// @brief Physical address of the referenced page frame.
    /// These are the 12 higher bits of the address (i.e. address / PAGE_SIZE)
    u32 page_frame : 20;
} mmu_pte_entry;

// The kernel's very own page directory
static __attribute__((__aligned__(PAGE_SIZE)))
mmu_pde_entry kernel_page_directory[MMU_PDE_COUNT];

// TODO: Dynamically allocate page table using frame allocator (1:1)

// The kernel's very own page tables
static __attribute__((__aligned__(PAGE_SIZE)))
mmu_pte_entry kernel_page_tables[MMU_PDE_COUNT][MMU_PTE_COUNT];

// TODO: Do not hard-code the content of CR3 to kernel's PD
//
// We set CR3 to be the kernel's page directory, AT ALL TIMES.
//
// This works for now since we only have ONE active tasks. But once
// we are able to execute multiple tasks at once, we should find another
// way to set its contentt to be that of the currently running process.
bool mmu_start_paging(void)
{
    static void *page_directory = kernel_page_directory;

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

    return true;
}

bool mmu_init(void)
{
    log_info("MMU", "Initializing MMU");

    // Mark all PDEs as "absent" (present = 0), and writable
    for (size_t entry = 0; entry < MMU_PDE_COUNT; entry++) {
        kernel_page_directory[entry] = (mmu_pde_entry){.writable = 1};
    }

    // For more simplicity, we identity map the content of our kernel.
    mmu_identity_map(align(KERNEL_CODE_START, PAGE_SIZE), KERNEL_CODE_END);
    // We also map the first 1M of physical memory, it will be reserved for
    // hardware structs.
    mmu_identity_map(0x0, 0x100000);

    return true;
}

bool mmu_map(u32 virtual, u32 pageframe)
{
    u16 pde_index = virtual >> 22;                     // bits 31-22
    u16 pte_index = (virtual >> 12) & ((1 << 10) - 1); // bits 21-12

    // TODO: Hard-coded to work with kernel page tables only
    //       This should take the pagedir/pagetables as input later on
    //
    // We also hardcode the pde/pte to be un-accessible when in user mode.
    // This will also cause an issue when reaching userspace later.

    if (!kernel_page_directory[pde_index].present) {
        kernel_page_directory[pde_index].page_table =
            (u32)&kernel_page_tables[pde_index] >> 12;
        kernel_page_directory[pde_index].present = 1;
    }

    if (kernel_page_tables[pde_index][pte_index].present) {
        log_err("MMU",
                "Allocating already allocated virtual address: " LOG_FMT_32,
                virtual);
        return false;
    }

    // Link the page table entry (virtual address) to the pageframe
    kernel_page_tables[pde_index][pte_index] = (mmu_pte_entry){
        .present = 1,
        .page_frame = pageframe >> 12,
        // TODO: hard-coded values
        .writable = 1,
        .user = 0,
    };

    return true;
}

void mmu_unmap(u32 virtual)
{
    u16 pde_index = virtual >> 22;                     // bits 31-22
    u16 pte_index = (virtual >> 12) & ((1 << 10) - 1); // bits 21-12

    if (!kernel_page_directory[pde_index].present)
        return;

    // TODO: Hard-coded to work with kernel pages only
    //       c.f. todo inside mmu_map

    // Erase the content of the page table entry
    *((volatile u32 *)&kernel_page_tables[pde_index][pte_index]) = 0x0;
}

void mmu_identity_map(uint32_t start, uint32_t end)
{
    for (; start < end; start += PAGE_SIZE) {
        mmu_map(start, start);
    }
}