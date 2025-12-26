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
 * ### Copy on Write (CoW)
 *
 * When duplicating an entire MMU, the process is delayed until necessary.
 * To do this we mark all writable pages as read-only, so that a pagefault
 * gets triggered the next time they are modified. The duplication of the
 * PDEs/PTEs is done during the pagefault's handling.
 *
 * This mechanism avoids duplicating pages that will never be accessed, which
 * is a common occurence when a process performs a combination of fork & exec.
 *
 * @{
 */

#define LOG_DOMAIN "mmu"

#include <kernel/cpu.h>
#include <kernel/error.h>
#include <kernel/interrupts.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/memory.h>
#include <kernel/mmu.h>
#include <kernel/pmm.h>
#include <kernel/process.h>
#include <kernel/spinlock.h>
#include <kernel/syscalls.h>
#include <kernel/types.h>
#include <kernel/vm.h>

#include <utils/bits.h>
#include <utils/compiler.h>

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static INTERRUPT_HANDLER_FUNCTION(page_fault);

/** Number of entries inside the page directory */
#define MMU_PDE_COUNT (1024)
/** Number of entries inside a single page table */
#define MMU_PTE_COUNT (1024)
/** First PDE corresponding to the kernel pages */
#define MMU_PDE_KERNEL_FIRST (767)
/** Number of PDE corresponding to kernel pages */
#define MMU_PDE_KERNEL_COUNT (MMU_PDE_COUNT - MMU_PDE_KERNEL_FIRST)

static spinlock_t mmu_lock = SPINLOCK_INIT;

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
    /// @brief Pageframe number of the referenced page table.
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
    /// @brief Pageframe number of the referenced page frame.
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

/**
 * Compute the virtual address of a page table when using recursive paging
 * @link https://medium.com/@connorstack/recursive-page-tables-ad1e03b20a85
 */
#define MMU_RECURSIVE_PAGE_DIRECTORY_ADDRESS \
    ((page_directory_t)FROM_PFN(0xFFFFF))
#define MMU_RECURSIVE_PAGE_TABLE_ADDRESS(_index) \
    ((page_table_t)FROM_PFN((0xFFC00 + (_index))))

// Page directory used when initializing the kernel
ALIGNED(PAGE_SIZE) mmu_pde_t kernel_startup_page_directory[MMU_PDE_COUNT];

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
 */
static inline void mmu_flush_tlb(vaddr_t addr)
{
    ASM("invlpg (%0)" ::"r"(addr) : "memory");
}

void mmu_load(paddr_t page_directory)
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
static void
mmu_offset_map(paddr_t start, paddr_t end, int64_t offset, int flags);

/** @brief Inititialize a new page directory
 *  @return The physical address of the new page_directory, 0 if error.
 */
paddr_t mmu_new(void)
{
    page_directory_t page_directory = MMU_RECURSIVE_PAGE_DIRECTORY_ADDRESS;
    page_directory_t new = vm_alloc(&kernel_address_space, PAGE_SIZE,
                                    VM_READ | VM_WRITE | VM_CLEAR);
    if (IS_ERR(new)) {
        log_err("Failed to allocate page for the new page directory");
        return -E_NOMEM;
    }

    // Copy kernel page table entries
    memcpy(&new[MMU_PDE_KERNEL_FIRST], &page_directory[MMU_PDE_KERNEL_FIRST],
           MMU_PDE_KERNEL_COUNT * sizeof(mmu_pte_t));

    paddr_t new_physical = mmu_find_physical((vaddr_t) new);
    // Setup recursive mapping
    new[MMU_PDE_COUNT - 1].page_table = TO_PFN(new_physical);

    // Unmap new page directory from current address space,
    // but do not release the pageframe
    page_get(address_to_page(new_physical));
    vm_free(&kernel_address_space, new);

    return new_physical;
}

void mmu_destroy(paddr_t mmu)
{
    pmm_free(mmu);
}

/*
 *
 */
static inline mmu_pde_t *mmu_get_active_page_directory(void)
{
    if (unlikely(!paging_enabled))
        return kernel_startup_page_directory;

    return MMU_RECURSIVE_PAGE_DIRECTORY_ADDRESS;
}

/*
 * Configure the caching policy on a page level. The caller must invalidate
 * the address' TLB entry after calling this function.
 */
static error_t
__mmu_set_policy(mmu_pde_t *page_directory, vaddr_t vaddr, int policy)
{
    mmu_decode_t address = {.raw = vaddr};
    mmu_pte_t *pte;
    bool pat = false;
    bool pwt = false;
    bool pcd = false;

    /* Sanitize input in case we were called from mmu_map(). */
    policy &= POLICY_UC | POLICY_WT | POLICY_WB | POLICY_WC;
    if (!policy)
        policy = POLICY_WB; /* Enable caching by default. */

    if (!page_directory[address.pde].present)
        return E_NOENT;

    pte = &MMU_RECURSIVE_PAGE_TABLE_ADDRESS(address.pde)[address.pte];
    switch (policy) {
    case POLICY_WB:
        break;
    case POLICY_WT:
        pwt = true;
        break;
    case POLICY_UC:
        pcd = true;
        break;
    case POLICY_WC:
        pat = true;
        break;

    default:
        WARN("invalid caching policy: %02x\n", policy);
        return E_INVAL;
    }

    if (pat && !cpu_has_feature(PAT)) {
        log_warn("unsupported policy: %02x (requires PAT support)", policy);
        return E_NOT_SUPPORTED;
    }

    pte->pat = pat;
    pte->pcd = pcd;
    pte->pwt = pwt;

    return E_SUCCESS;
}

/*
 *
 */
error_t mmu_set_policy(vaddr_t vaddr, mmu_policy_t policy)
{
    mmu_pde_t *page_directory;
    error_t err;

    page_directory = mmu_get_active_page_directory();
    err = __mmu_set_policy(page_directory, vaddr, policy);
    if (err)
        return err;

    mmu_flush_tlb(vaddr);
    return E_SUCCESS;
}

/*
 *
 */
error_t mmu_set_policy_range(vaddr_t range_start, size_t range_size,
                             mmu_policy_t policy)
{
    error_t ret = E_SUCCESS;

    range_size = align_down(range_size, PAGE_SIZE);
    for (size_t off = 0; off < range_size; off += PAGE_SIZE) {
        /* Keep going when an error happens but return the first error code. */
        error_t err = mmu_set_policy(range_start + off, policy);
        if (err && !ret)
            ret = err;
    }

    return ret;
}

// TODO: We do not have a way to quickly map and access an arbitrary physical
// address.
//       This prevents us from cloning an arbitrary MMU instance. This is the
//       reason why this function currently only takes in the destination MMU as
//       a parameter.
void mmu_clone(paddr_t destination)
{
    page_directory_t src_page_directory;
    page_directory_t dst_page_directory;
    page_table_t page_table;
    struct page *page;

    src_page_directory = MMU_RECURSIVE_PAGE_DIRECTORY_ADDRESS;
    dst_page_directory = vm_alloc_at(&kernel_address_space, destination,
                                     PAGE_SIZE, VM_READ | VM_WRITE);

    for (size_t i = 0; i < MMU_PDE_KERNEL_FIRST; ++i) {

        dst_page_directory[i] = src_page_directory[i];
        if (!dst_page_directory[i].present)
            continue;

        page = page_get(pfn_to_page(dst_page_directory[i].page_table));

        // Setup PT for copy-on-write
        if (dst_page_directory[i].writable) {
            page->flags |= PAGE_COW;
            dst_page_directory[i].writable = false;
            src_page_directory[i].writable = false;
        }

        // Setup PTEs for copy-on-write
        page_table = MMU_RECURSIVE_PAGE_TABLE_ADDRESS(i);
        for (size_t j = 0; j < MMU_PTE_COUNT; ++j) {
            page = page_get(pfn_to_page(page_table[j].page_frame));
            if (page_table[j].writable) {
                page->flags |= PAGE_COW;
                page_table->writable = false;
            }
        }
    }

    vm_free(&kernel_address_space, dst_page_directory);
}

bool mmu_map(vaddr_t virtual, paddr_t pageframe, int flags)
{
    mmu_decode_t address = {.raw = virtual};
    page_directory_t page_directory = mmu_get_active_page_directory();
    page_table_t page_table;
    bool new_page_table = false;

    if (virtual % PAGE_SIZE)
        return false;

    if (!page_directory[address.pde].present) {
        u32 page_table = pmm_allocate();
        page_directory[address.pde] = (mmu_pde_t){
            .present = 1,
            .page_table = TO_PFN(page_table),
            .writable = 1,
            .user = 1,
        };
        new_page_table = true;
    }

    // Virtual address of the corresponding page table (physical if CR0.PG=0)
    if (paging_enabled) {
        page_table = MMU_RECURSIVE_PAGE_TABLE_ADDRESS(address.pde);
    } else {
        page_table = (page_table_t)FROM_PFN(
            page_directory[address.pde].page_table);
    }

    // clear the page table to avoid having residual values corrupt our mappings
    if (new_page_table)
        memset((void *)page_table, 0, PAGE_SIZE);

    if (page_table[address.pte].present) {
        log_err("Allocating already allocated virtual address: " FMT32,
                virtual);
        return false;
    }

    // Link the page table entry (virtual address) to the pageframe
    page_table[address.pte] = (mmu_pte_t){
        .present = 1,
        .page_frame = TO_PFN(pageframe),
        .writable = boolean(flags & PROT_WRITE),
        .user = !(flags & PROT_KERNEL),
    };

    /* No need to flush since the entry has not been cached yet. */
    __mmu_set_policy(page_directory, virtual, flags);

    return true;
}

bool mmu_map_range(vaddr_t virtual, paddr_t physical, size_t size, int flags)
{
    size_t range;

    if (size % PAGE_SIZE) {
        log_warn("map_range: the range's size must be page-aligned");
        return false;
    }

    for (range = 0; range < size; range += PAGE_SIZE) {
        if (!mmu_map(virtual + range, physical + range, flags))
            break;
    }

    if (range < size) {
        mmu_unmap_range(virtual, virtual + range);
        return false;
    }

    return true;
}

/** Duplicate the content of a CoW page */
static paddr_t __duplicate_cow_page(void *orig)
{
    paddr_t phys_new;
    void *new;

    phys_new = pmm_allocate();
    if (phys_new == PMM_INVALID_PAGEFRAME)
        return PMM_INVALID_PAGEFRAME;

    new = vm_alloc_at(&kernel_address_space, phys_new, PAGE_SIZE, VM_WRITE);
    if (IS_ERR(new)) {
        pmm_free(phys_new);
        return PMM_INVALID_PAGEFRAME;
    }

    memcpy(new, orig, PAGE_SIZE);
    vm_free(&kernel_address_space, new);

    return phys_new;
}

paddr_t mmu_unmap(vaddr_t virtual)
{
    mmu_decode_t address = {.raw = virtual};
    page_table_t page_table;
    paddr_t physical;
    struct page *page;
    mmu_pde_t *pde;
    mmu_pte_t *pte;

    pde = &MMU_RECURSIVE_PAGE_DIRECTORY_ADDRESS[address.pde];
    page_table = MMU_RECURSIVE_PAGE_TABLE_ADDRESS(address.pde);
    pte = &page_table[address.pte];

    if (!pde->present || !pte->present)
        return PMM_INVALID_PAGEFRAME;

    page = pfn_to_page(pde->page_table);
    if (page_is_cow(page)) {
        physical = __duplicate_cow_page(page_table);
        page_put(page);
        pde->page_table = TO_PFN(physical);
        pde->writable = true;
        mmu_flush_tlb((vaddr_t)pte);
    }

    // Erase the content of the page table entry
    page = pfn_to_page(pte->page_frame);
    *((volatile u32 *)pte) = 0x0;

    mmu_flush_tlb(virtual);

    return page_address(page);
}

void mmu_unmap_range(vaddr_t start, vaddr_t end)
{
    for (; start < end; start += PAGE_SIZE)
        mmu_unmap(start);
}

static void
mmu_offset_map(paddr_t start, paddr_t end, int64_t offset, int flags)
{
    for (; start < end; start += PAGE_SIZE) {
        mmu_map(start + offset, start, flags);
    }
}

void mmu_identity_map(paddr_t start, paddr_t end, int flags)
{
    mmu_offset_map(start, end, 0, flags);
}

paddr_t mmu_find_physical(vaddr_t virtual)
{
    mmu_decode_t address = {.raw = virtual};

    if (!MMU_RECURSIVE_PAGE_DIRECTORY_ADDRESS[address.pde].present)
        return -E_INVAL;

    page_table_t page_table = MMU_RECURSIVE_PAGE_TABLE_ADDRESS(address.pde);
    if (!page_table[address.pte].present)
        return -E_INVAL;

    return FROM_PFN(page_table[address.pte].page_frame) + address.offset;
}

error_t mmu_copy_on_write(vaddr_t addr)
{
    mmu_decode_t address = {.raw = addr};
    mmu_pde_t *pde = &MMU_RECURSIVE_PAGE_DIRECTORY_ADDRESS[address.pde];
    mmu_pte_t *pte;
    struct page *page;
    paddr_t duplicated;
    error_t ret = E_SUCCESS;

    if (!pde->present)
        return E_NOENT;

    spinlock_acquire(&mmu_lock);

    page = pfn_to_page(pde->page_table);
    if (!pde->writable) {
        if (!(page->flags & PAGE_COW)) {
            ret = E_PERM; // Regular read-only pagetable
            goto release_lock;
        }

        // Duplicate pagetable if currently shared
        if (page->refcount > 1) {
            duplicated = __duplicate_cow_page(
                MMU_RECURSIVE_PAGE_TABLE_ADDRESS(address.pde));
            if (duplicated == PMM_INVALID_PAGEFRAME)
                return E_NOMEM;
            // Update pagetable's address & release old reference
            pde->page_table = TO_PFN(duplicated);
            page_put(page);
            page = pfn_to_page(pde->page_table);
        }

        pde->writable = true;
        page->flags &= ~PAGE_COW;
    }

    pte = &MMU_RECURSIVE_PAGE_TABLE_ADDRESS(address.pde)[address.pte];
    if (!pte->present) {
        ret = E_NOENT;
        goto release_lock;
    }

    page = pfn_to_page(pte->page_frame);
    if (!(page->flags & PAGE_COW)) {
        ret = E_PERM; // Regular read-only page
        goto release_lock;
    }

    // Same as for the pagetable, duplicate it if shared
    if (page->refcount > 1) {
        duplicated = __duplicate_cow_page((void *)align_down(addr, PAGE_SIZE));
        if (duplicated == PMM_INVALID_PAGEFRAME)
            return E_NOMEM;
        // Update page's address & release old reference
        pte->page_frame = TO_PFN(duplicated);
        page_put(page);
        page = pfn_to_page(pte->page_frame);
    }

    pte->writable = true;
    page->flags &= ~PAGE_COW;

    mmu_flush_tlb(addr);

release_lock:
    spinlock_release(&mmu_lock);
    return ret;
}

/* Memory types used to configure the MTRR and PAT tables. */
enum memory_type {
    MEM_UC = 0x00,
    MEM_WC = 0x01,
    MEM_WT = 0x04,
    MEM_WP = 0x05,
    MEM_WB = 0x06,
    MEM_UC_MINUS = 0x07, /* Valid only for the PAT table. */
};

/*
 * Fill the page attribute table.
 */
static void mmu_init_pat(void)
{
    u64 pat = 0;

    if (!cpu_has_feature(PAT)) {
        log_info("PAT not present on this platform");
        return;
    }

#define PAT(n, val) (((u64)val & 0xff) << (n * 8))

    /* Configure a PAT entry for each caching related bit inside a PTE. */
    pat |= PAT(0, MEM_WB);
    pat |= PAT(1, MEM_WT);
    pat |= PAT(2, MEM_UC);
    pat |= PAT(4, MEM_WC);

    wrmsr(MSR_PAT, pat);

#undef PATn
}

/*
 *
 */
static void mmu_init_mtrr(void)
{
    if (!cpu_has_feature(MTRR)) {
        log_info("MTRR not present this platform");
        return;
    }

    /* TODO add support for MTRRs. */
    not_implemented("MTRR");
}

/*
 * Initialize the content of the page directory.
 */
static void mmu_init_page_directory(paddr_t page_directory)
{
    // Mark all PDEs as "absent" (present = 0), and writable
    for (size_t entry = 0; entry < MMU_PDE_COUNT; entry++) {
        kernel_startup_page_directory[entry] = (mmu_pde_t){
            .present = false,
            .writable = true,
        };
    }

    // Setup recursive page tables
    // @link https://medium.com/@connorstack/recursive-page-tables-ad1e03b20a85
    kernel_startup_page_directory[MMU_PDE_COUNT - 1].present = 1;
    kernel_startup_page_directory[MMU_PDE_COUNT - 1].page_table = TO_PFN(
        page_directory);

    // We remap our higher-half kernel.
    // The addresses over 0xC0000000 will point to our kernel's code (0x00000000
    // in physical)
    //
    // FIXME: The kernel code is currently accessible from userland.
    //        This MUST be changed back to add PROT_KERNEL, but for now
    //        we keep it as is, since it is the only way for us to test
    //        our userland implementation until we port our first programs
    //        (soon hopefully)
    mmu_offset_map(0, KERNEL_HIGHER_HALF_PHYSICAL(KERNEL_CODE_END),
                   KERNEL_HIGHER_HALF_OFFSET, PROT_EXEC | PROT_READ);
}

bool mmu_init(void)
{
    paddr_t page_directory;
    paddr_t page_table;
    u32 val;

    if (paging_enabled) {
        log_warn("Trying to re-enable paging. Skipping.");
        return false;
    }

    interrupts_set_handler(PAGE_FAULT, INTERRUPT_HANDLER(page_fault), NULL);

    /* Initialize caching structures. */
    mmu_init_pat();
    mmu_init_mtrr();

    page_directory = KERNEL_HIGHER_HALF_PHYSICAL(kernel_startup_page_directory);
    kernel_address_space.mmu = page_directory;

    mmu_init_page_directory(page_directory);

    /*
     * Enable 32b mode paging.
     */

    mmu_load(page_directory);

    val = read_cr4();
    val &= ~CR4_PAE;
    write_cr4(val);

    val = read_cr0();
    val |= CR0_PG;
    write_cr0(val);

    paging_enabled = true;

    /*
     * Pre-allocate all shared kernel page table entries.
     *
     * We MUST allocate them now for them to be present inside the IDLE
     * task's page table.
     */

    for (size_t i = MMU_PDE_KERNEL_FIRST; i < MMU_PDE_COUNT - 1; i++) {
        if (kernel_startup_page_directory[i].present)
            continue;
        page_table = pmm_allocate();
        kernel_startup_page_directory[i].page_table = TO_PFN(page_table);
        kernel_startup_page_directory[i].present = true;
        kernel_startup_page_directory[i].user = false;
        memset(MMU_RECURSIVE_PAGE_TABLE_ADDRESS(i), 0, PAGE_SIZE);
    }

    return true;
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

static INTERRUPT_HANDLER_FUNCTION(page_fault)
{
    // The CR2 register holds the virtual address which caused the Page Fault
    void *faulty_address = (void *)read_cr2();
    interrupt_frame *frame = data;
    page_fault_error error = *(page_fault_error *)&frame->error;
    bool is_cow = error.present && error.write;
    struct address_space *as;

    if (!error.present || is_cow) {
        as = IS_KERNEL_ADDRESS(faulty_address) ? &kernel_address_space
                                               : current->process->as;
        if (unlikely(!as)) {
            log_err("page_fault: address space is NULL");
            goto page_fault_panic;
        }

        if (!address_space_fault(as, faulty_address, is_cow))
            return E_SUCCESS;
    }

page_fault_panic:
    PANIC("PAGE FAULT at " FMT32 ": %s access on a %s page %s", faulty_address,
          error.write ? "write" : "read",
          error.present ? "protected" : "non-present",
          error.user ? "while in user-mode" : "");
}
