#define LOG_DOMAIN "i686"

#include <kernel/interrupts.h>
#include <kernel/init.h>
#include <kernel/memory.h>
#include <kernel/logger.h>

#include <kernel/arch/i686/devices/pic.h>
#include <kernel/arch/i686/gdt.h>
#include <kernel/arch/i686/apic.h>

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
    error_t err;

    /*
     * Try to use the more modern APIC interrupt controller.
     * If we failed to do so, we fallback to using the original PIC.
     */
    err = apic_init();
    if (err) {
        log_info("Failed to setup APIC, falling back to old 8259 PIC.");
        apic_disable();
        pic_init();
    }

    return E_SUCCESS;
}

DECLARE_INITCALL(INIT_BOOTSTRAP, arch_bootstrap);
DECLARE_INITCALL(INIT_EARLY, arch_early);
