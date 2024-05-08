/**
 * @file kernel/arch/i686/memory/mmu.c
 *
 * @defgroup mmu_x86 MMU - X86 specific
 * @ingroup mmu
 * @ingroup x86
 *
 * # MMU - x86
 *
 * Arch dependent implementation of the MMU interface.
 *
 * ## Design
 *
 * The x86 CPU family uses page tables to translate virtual addresses into
 * physical ones.
 *
 * Each page table containes 1024 entries (PTE), each corresponding to a single
 * page in the virtual address space. Each PTE contains the physical address it
 * is mapped to, as well as metadata for this page. These metadata can be used
 * to modify the CPU's comportement when accessing an address inside this page
 * (security restriction, cache policy, ...).
 *
 * There exists different levels of Page Tables (PT). Page Tables can also
 * contain links to other page tables, to allow accessing a larger virtual
 * address space.
 *
 * @note In our current implementation we only use one PT level => 32bits
 * virtual address space.
 *
 * At the root of all this is the Page Directory (PD). Similarily to PTs, the PD
 * holds the addresses of page tables, as well as similar metadatas. The CPU
 * keeps the address of the PD for the running process inside the CR3 register.
 *
 * ## Implementation
 *
 * ### Recursvie Mapping
 *
 * To allow us to easily edit the content of the page table entries, even once
 * we have switched on paging, we map a the last entry in the page directory to
 * itself. This allows us to be able to compute the address of a given page
 * table, and manually edit its content without adding an otherwise necessary
 * page table entry ... which would require mapping antother PTE (chicken and
 * egg).
 *
 * @see https://medium.com/@connorstack/recursive-page-tables-ad1e03b20a85
 *
 * @{
 */

#include <kernel/cpu.h>
#include <kernel/logger.h>
#include <kernel/memory.h>
#include <kernel/mmu.h>
#include <kernel/pmm.h>
#include <kernel/types.h>

#include <utils/bits.h>
#include <utils/compiler.h>

#include <stdbool.h>
#include <stddef.h>

/** Number of entries inside the page directory */
#define MMU_PDE_COUNT (1024)
/** Number of entries inside a single page table */
#define MMU_PTE_COUNT (1024)

/**
 * Compute the virtual address of a page table when using recursive paging
 * @link https://medium.com/@connorstack/recursive-page-tables-ad1e03b20a85
 */
#define MMU_RECURSIVE_PAGE_TABLE_ADDRESS(_index) ((0xFFC00 + (_index)) << 12)

/**
 * @struct mmu_pde Page Directory entry
 * @see Intel developper manual - Table 4-5
 */
typedef struct PACKED mmu_pde {
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
} mmu_pde_t;

/**
 * @struct mmu_pte Page Table entry
 * @info we use 32-bit page tables that map 4-KiB pages
 * @see Intel developper manual - Table 4-6
 */
typedef struct PACKED mmu_pte {
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
} mmu_pte_t;

/** Compute the 12 higher bits of a page's address (i.e. address / PAGE_SIZE) */
#define MMU_PAGE_ADDRESS(_page) (((u32)_page) >> 12)

/**
 * @brief Flush the translation buffer
 * @function mmu_flush_tlb
 *
 * For efficiency, the result of the translations are cached by the CPU. This
 * means that we need to invalidate the cache if we want new modifications made
 * to the PD to be taken into account.
 *
 * @param tlb_entry Virtual address whose translation we want to update
 */
static inline void mmu_flush_tlb(u32 tlb_entry)
{
    ASM("invlpg (%0)" ::"r"(tlb_entry) : "memory");
}

// The kernel's very own page directory
static __attribute__((__aligned__(PAGE_SIZE)))
mmu_pde_t kernel_page_directory[MMU_PDE_COUNT];

// Keep track whether paging has been fully enabled.
// This doesn't count temporarily enabling it to jump into higher half.
static bool paging_enabled = false;

// TODO: Do not hard-code the content of CR3 to kernel's PD
//
// We set CR3 to be the kernel's page directory, AT ALL TIMES.
//
// This works for now since we only have ONE active tasks. But once
// we are able to execute multiple tasks at once, we should find another
// way to set its contentt to be that of the currently running process.
bool mmu_start_paging(void)
{
    static void *page_directory =
        (void *)KERNEL_HIGHER_HALF_PHYSICAL(kernel_page_directory);

    // Set CR3 to point to our page directory
    write_cr3((u32)page_directory);

    // According to 4.3, to activate 32-bit mode paging we must:
    // 1. set CR4.PAE to 0 (de-activate PAE)
    u32 cr4 = read_cr4();
    write_cr4(BIT_CLEAR(cr4, 5)); // PAE = bit 6

    // 2. set CR0.PG to 1  (activate paging)
    u32 cr0 = read_cr0();
    write_cr0(BIT_SET(cr0, 31)); // PG = bit 32

    paging_enabled = true;

    return true;
}

/*
 * @brief Remap an address range given an offset.
 *
 * example:
 *     mmu_offset_map(0x0000, 0x00FF, 0xFF) will remap the physical range
 *     [0x0000; 0x00FF] to the virtual range [0xFF00; 0xFFFF]
 */
static void mmu_offset_map(paddr_t start, paddr_t end, int64_t offset);

bool mmu_init(void)
{
    log_info("MMU", "Initializing MMU");

    // Mark all PDEs as "absent" (present = 0), and writable
    for (size_t entry = 0; entry < MMU_PDE_COUNT; entry++) {
        kernel_page_directory[entry] = (mmu_pde_t){.writable = 1};
    }

    // Setup recursive page tables
    // @link https://medium.com/@connorstack/recursive-page-tables-ad1e03b20a85
    kernel_page_directory[MMU_PDE_COUNT - 1].present = 1;
    kernel_page_directory[MMU_PDE_COUNT - 1].page_table =
        MMU_PAGE_ADDRESS(KERNEL_HIGHER_HALF_PHYSICAL(kernel_page_directory));

    // We remap our higher-half kernel.
    // The addresses over 0xC0000000 will point to our kernel's code (0x00000000
    // in physical)
    mmu_offset_map(KERNEL_HIGHER_HALF_PHYSICAL(KERNEL_CODE_START),
                   KERNEL_HIGHER_HALF_PHYSICAL(KERNEL_CODE_END),
                   KERNEL_HIGHER_HALF_OFFSET);

    // Identity map the first MB, since it contains hardcoded addresses we still
    // use (console buffer for example).
    //
    // TODO: Check for possible alternatives? (MMIO?, map only what we need?)
    mmu_identity_map(0x0, 0x100000);

    return true;
}

bool mmu_map(vaddr_t virtual, vaddr_t pageframe)
{
    u16 pde_index = virtual >> 22;                     // bits 31-22
    u16 pte_index = (virtual >> 12) & ((1 << 10) - 1); // bits 21-12

    // TODO: ASSERT alignment

    // TODO: Hard-coded to work with kernel page tables only
    //       This should take the pagedir/pagetables as input later on
    //
    // We also hardcode the pde/pte to be un-accessible when in user mode.
    // This will also cause an issue when reaching userspace later.

    if (!kernel_page_directory[pde_index].present) {
        u32 page_table = pmm_allocate(PMM_MAP_KERNEL);
        kernel_page_directory[pde_index].page_table =
            MMU_PAGE_ADDRESS(page_table);
        kernel_page_directory[pde_index].present = 1;
    }

    // Virtual address of the corresponding page table (physical if CR0.PG=0)
    mmu_pte_t *page_table;

    if (paging_enabled) {
        page_table = (mmu_pte_t *)MMU_RECURSIVE_PAGE_TABLE_ADDRESS(pde_index);
    } else {
        page_table = (mmu_pte_t *)KERNEL_HIGHER_HALF_VIRTUAL(
            kernel_page_directory[pde_index].page_table << 12);
    }

    if (page_table[pte_index].present) {
        log_err("MMU",
                "Allocating already allocated virtual address: " LOG_FMT_32,
                virtual);
        return false;
    }

    // Link the page table entry (virtual address) to the pageframe
    page_table[pte_index] = (mmu_pte_t){
        .present = 1,
        .page_frame = MMU_PAGE_ADDRESS(pageframe),
        // TODO: hard-coded values
        .writable = 1,
        .user = 0,
    };

    return true;
}

paddr_t mmu_unmap(vaddr_t virtual)
{
    u16 pde_index = virtual >> 22;                     // bits 31-22
    u16 pte_index = (virtual >> 12) & ((1 << 10) - 1); // bits 21-12

    // TODO: Hard-coded to work with kernel pages only
    //       c.f. todo inside mmu_map

    if (!kernel_page_directory[pde_index].present)
        return 0;

    // Erase the content of the page table entry
    mmu_pte_t *page_table =
        (mmu_pte_t *)MMU_RECURSIVE_PAGE_TABLE_ADDRESS(pde_index);
    paddr_t physical = page_table->page_frame << 12;
    *((volatile u32 *)&page_table[pte_index]) = 0x0;

    mmu_flush_tlb(virtual);

    return physical;
}

static void mmu_offset_map(paddr_t start, paddr_t end, int64_t offset)
{
    for (; start < end; start += PAGE_SIZE) {
        mmu_map(start + offset, start);
    }
}

void mmu_identity_map(paddr_t start, paddr_t end)
{
    mmu_offset_map(start, end, 0);
}
