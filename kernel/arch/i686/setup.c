#include <kernel/interrupts.h>
#include <kernel/init.h>
#include <kernel/memory.h>
#include <kernel/logger.h>

#include <kernel/arch/i686/devices/pic.h>
#include <kernel/arch/i686/gdt.h>

#include <utils/constants.h>

static error_t arch_bootstrap(void)
{
    size_t kernel_size = KERNEL_CODE_END - KERNEL_CODE_START;

    // The bootstrap page tables MUST fit the whole kernel.
    // If this is not the case, we risk writing over our kernel's
    // code without even noticing it.
    //
    // If this is ever the case, you should update the bootstrap
    // tables' size inside crt0.S, and reflect this change inside
    // this condition.
    if (kernel_size >= 16 * MB)
        PANIC("Kernel has become too big !");

    gdt_init();
    gdt_log();

    return E_SUCCESS;
}

static error_t arch_early(void)
{
    pic_reset();

    return E_SUCCESS;
}

DECLARE_INITCALL(INIT_BOOTSTRAP, arch_bootstrap);
DECLARE_INITCALL(INIT_EARLY, arch_early);
