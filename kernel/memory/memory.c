#define LOG_DOMAIN "memory"

#include <kernel/init.h>
#include <kernel/logger.h>
#include <kernel/mmu.h>
#include <kernel/pmm.h>
#include <kernel/vm.h>

#include <multiboot.h>

void memory_init(struct multiboot_info *mbt)
{
    log_info("Initializing pageframe allocator");
    if (!pmm_init(mbt))
        PANIC("Failed to initialize the physical memory manager");

    log_info("Initializing MMU");
    if (!mmu_init())
        PANIC("Failed to initialize virtual address space");

    address_space_init(&kernel_address_space);
}
