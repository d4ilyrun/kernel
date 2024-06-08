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
#include <kernel/error.h>
#include <kernel/logger.h>
#include <kernel/memory.h>
#include <kernel/mmu.h>
#include <kernel/pmm.h>
#include <kernel/process.h>
#include <kernel/syscalls.h>
#include <kernel/types.h>

#include <utils/bits.h>
#include <utils/compiler.h>

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/** Number of entries inside the page directory */
#define MMU_PDE_COUNT (1024)
/** Number of entries inside a single page table */
#define MMU_PTE_COUNT (1024)
/** First PDE corresponding to the kernel pages */
#define MMU_PDE_KERNEL_FIRST (767)
/** Number of PDE corresponding to kernel pages */
#define MMU_PDE_KERNEL_COUNT (MMU_PDE_COUNT - MMU_PDE_KERNEL_FIRST)

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

typedef mmu_pde_t *page_directory_t;
typedef mmu_pte_t *page_table_t;

/** Representation of a 32bit virtual address from the MMU's POV */
typedef union {

    u32 raw;
    struct {
        u32 offset : 12;
        u32 pte : 10;
        u32 pde : 10;
    };

} mmu_decode_t;

static_assert(sizeof(mmu_decode_t) == sizeof(u32));

/** Convert pageframe address to page frame number */
#define TO_PFN(_pageframe) (((u32)(_pageframe)) >> 12)
/** Convert pageframe number to pageframe address */
#define FROM_PFN(_pageframe) ((_pageframe) << 12)

/**
 * Compute the virtual address of a page table when using recursive paging
 * @link https://medium.com/@connorstack/recursive-page-tables-ad1e03b20a85
 */
#define MMU_RECURSIVE_PAGE_DIRECTORY_ADDRESS \
    ((page_directory_t)FROM_PFN(0xFFFFF))
#define MMU_RECURSIVE_PAGE_TABLE_ADDRESS(_index) \
    ((page_table_t)FROM_PFN((0xFFC00 + (_index))))

// Page directory used when initializing the kernel
static __attribute__((__aligned__(PAGE_SIZE)))
mmu_pde_t kernel_startup_page_directory[MMU_PDE_COUNT];

// Keep track whether paging has been fully enabled.
// This doesn't count temporarily enabling it to jump into higher half.
static bool paging_enabled = false;

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

void mmu_load_page_directory(paddr_t page_directory)
{
    write_cr3(page_directory);
}

/*
 * @brief Remap an address range given an offset.
 *
 * example:
 *     mmu_offset_map(0x0000, 0x00FF, 0xFF) will remap the physical range
 *     [0x0000; 0x00FF] to the virtual range [0xFF00; 0xFFFF]
 */
static void mmu_offset_map(paddr_t start, paddr_t end, int64_t offset,
                           int prot);

bool mmu_init(void)
{
    if (paging_enabled) {
        log_warn("MMU", "Trying to re-enable paging. Skipping.");
        return false;
    }

    // Initialize the kernel's page directory
    kernel_startup_process.context.cr3 =
        KERNEL_HIGHER_HALF_PHYSICAL(kernel_startup_page_directory);

    // Mark all PDEs as "absent" (present = 0), and writable
    for (size_t entry = 0; entry < MMU_PDE_COUNT; entry++) {
        kernel_startup_page_directory[entry] = (mmu_pde_t){.writable = 1};
    }

    // Setup recursive page tables
    // @link https://medium.com/@connorstack/recursive-page-tables-ad1e03b20a85
    kernel_startup_page_directory[MMU_PDE_COUNT - 1].present = 1;
    kernel_startup_page_directory[MMU_PDE_COUNT - 1].page_table =
        TO_PFN(kernel_startup_process.context.cr3);

    // We remap our higher-half kernel.
    // The addresses over 0xC0000000 will point to our kernel's code (0x00000000
    // in physical)
    mmu_offset_map(KERNEL_HIGHER_HALF_PHYSICAL(KERNEL_CODE_START),
                   KERNEL_HIGHER_HALF_PHYSICAL(KERNEL_CODE_END),
                   KERNEL_HIGHER_HALF_OFFSET, PROT_EXEC | PROT_READ);

    // Identity map the first MB, since it contains hardcoded addresses we still
    // use (console buffer for example).
    //
    // TODO: Check for possible alternatives? (MMIO?, map only what we need?)
    mmu_identity_map(0x0, 0x100000, PROT_READ | PROT_WRITE);

    mmu_load_page_directory(kernel_startup_process.context.cr3);

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

/** @brief Inititialize a new page directory
 *  @return The physical address of the new page_directory, 0 if error.
 */
paddr_t mmu_new_page_directory(void)
{
    page_directory_t new =
        mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_CLEAR);
    if (new == NULL) {
        log_err("MMU", "Failed to allocate page for the new page directory");
        return -E_NOMEM;
    }

    page_directory_t page_directory = MMU_RECURSIVE_PAGE_DIRECTORY_ADDRESS;

    // Copy kernel page table entries
    memcpy(&new[MMU_PDE_KERNEL_FIRST], &page_directory[MMU_PDE_KERNEL_FIRST],
           MMU_PDE_KERNEL_COUNT * sizeof(mmu_pte_t));

    paddr_t new_physical = mmu_find_physical((vaddr_t) new);
    new[MMU_PDE_COUNT - 1].page_table =
        TO_PFN(new_physical); // Setup recursive mapping

    // Unmap new page directory from current address space, but do not release
    // the pageframe
    vmm_free(current_process->vmm, (vaddr_t) new, PAGE_SIZE);
    mmu_unmap((vaddr_t) new);

    return new_physical;
}

bool mmu_map(vaddr_t virtual, vaddr_t pageframe, int prot)
{
    mmu_decode_t address = {.raw = virtual};

    if (virtual % PAGE_SIZE)
        return false;

    // TODO: We hardcode the pde/pte to be un-accessible when in user mode.
    // This will also cause an issue when reaching userspace later.

    page_directory_t page_directory;
    page_table_t page_table;
    bool new_page_table = false;

    if (paging_enabled)
        page_directory = MMU_RECURSIVE_PAGE_DIRECTORY_ADDRESS;
    else
        page_directory = kernel_startup_page_directory;

    if (!page_directory[address.pde].present) {
        u32 page_table = pmm_allocate(PMM_MAP_KERNEL);
        page_directory[address.pde].page_table = TO_PFN(page_table);
        page_directory[address.pde].present = 1;
        new_page_table = true;
    }

    // Virtual address of the corresponding page table (physical if CR0.PG=0)
    if (paging_enabled) {
        page_table = MMU_RECURSIVE_PAGE_TABLE_ADDRESS(address.pde);
    } else {
        page_table =
            (page_table_t)FROM_PFN(page_directory[address.pde].page_table);
    }

    // clear the page table to avoid having residual values corrupt our mappings
    if (new_page_table)
        memset((void *)page_table, 0, PAGE_SIZE);

    if (page_table[address.pte].present) {
        log_err("MMU",
                "Allocating already allocated virtual address: " LOG_FMT_32,
                virtual);
        return false;
    }

    // Link the page table entry (virtual address) to the pageframe
    page_table[address.pte] = (mmu_pte_t){
        .present = 1,
        .page_frame = TO_PFN(pageframe),
        // cannot disable reading from x86 pages pages
        .writable = boolean(prot & PROT_WRITE),
        .user = 0,
    };

    return true;
}

paddr_t mmu_unmap(vaddr_t virtual)
{
    mmu_decode_t address = {.raw = virtual};

    if (!MMU_RECURSIVE_PAGE_DIRECTORY_ADDRESS[address.pde].present)
        return PMM_INVALID_PAGEFRAME;

    page_table_t page_table = MMU_RECURSIVE_PAGE_TABLE_ADDRESS(address.pde);
    if (!page_table[address.pte].present)
        return PMM_INVALID_PAGEFRAME;

    // Erase the content of the page table entry
    paddr_t physical = FROM_PFN(page_table[address.pte].page_frame);
    *((volatile u32 *)&page_table[address.pte]) = 0x0;

    mmu_flush_tlb(virtual);

    return physical;
}

static void mmu_offset_map(paddr_t start, paddr_t end, int64_t offset, int prot)
{
    for (; start < end; start += PAGE_SIZE) {
        mmu_map(start + offset, start, prot);
    }
}

void mmu_identity_map(paddr_t start, paddr_t end, int prot)
{
    mmu_offset_map(start, end, 0, prot);
}

paddr_t mmu_find_physical(vaddr_t virtual)
{
    mmu_decode_t address = {.raw = virtual};

    if (!MMU_RECURSIVE_PAGE_DIRECTORY_ADDRESS[address.pde].present)
        return -E_INVAL;

    page_table_t page_table = MMU_RECURSIVE_PAGE_TABLE_ADDRESS(address.pde);
    if (!page_table[address.pte].present)
        return -E_INVAL;

    return FROM_PFN(page_table[address.pte].page_frame);
}
