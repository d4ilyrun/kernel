#define LOG_DOMAIN "apic"

#include <kernel/error.h>
#include <kernel/logger.h>
#include <kernel/pmm.h>
#include <kernel/types.h>
#include <kernel/vm.h>

#include <utils/bits.h>

void apic_disable(void)
{
    uint64_t msr;

    if (!cpu_has_msr())
        return;

    log_info("disabling APIC");

    /*
     * Bit 11 of the APIC_BASE MSR enables or disables the local APIC.
     * See Intel SDM 12.4.3 & 12.4.4
     */
    msr = rdmsr(MSR_IA32_APIC_BASE);
    msr = BIT_CLEAR(msr, 11);
    wrmsr(MSR_IA32_APIC_BASE, msr);
}

error_t apic_init(void)
{
    paddr_t apic_base;
    void *apic_regs;

    /*
     * Old hardware may not include an APIC.
     */
    if (!(cpuid_edx(CPUID_LEAF_GETFEATURES) & BIT(9))) {
        log_info("CPU feature do not include APIC");
        return E_NOT_SUPPORTED;
    }

    /*
     * Same for MSRs, which are required controlling the APIC.
     */
    if (!cpu_has_msr()) {
        log_info("CPU does not support MSRs");
        return E_NOT_SUPPORTED;
    }

    /*
     * Bit 12-35 specify the base hardware address of the APIC registers.
     * This value is extended by 12 bits at the low end, which make it
     * effectively aligned onto a pageframe.
     *
     * FIXME: For correct APIC operation, this address space must be mapped
     * to an area of memory that has been designated as strong uncacheable.
     *
     * See Intel SDM 12.4.4
     */
    apic_base = align_down(rdmsr(MSR_IA32_APIC_BASE), PAGE_SIZE);
    apic_regs = vm_alloc_at(&kernel_address_space, apic_base, PAGE_SIZE,
                            VM_KERNEL_RW);
    if (!apic_regs) {
        log_err("failed to remap APIC registers");
        return E_NOMEM;
    }

    return E_SUCCESS;
}
