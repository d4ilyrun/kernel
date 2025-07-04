#define LOG_DOMAIN "apic"

#include <kernel/error.h>
#include <kernel/logger.h>
#include <kernel/pmm.h>
#include <kernel/types.h>
#include <kernel/vm.h>

#include <utils/bits.h>

static void *apic_registers = NULL;

/*
 * APIC register offsets
 */
#define APIC_EOI 0xB0

static inline u32 apic_readl(u16 reg)
{
    return *(u32 *)(apic_registers + reg);
}

static inline void apic_writel(u32 val, u16 reg)
{
    *(u32 *)(apic_registers + reg) = val;
}

/*
 * Signal end of an APIC interrupt routine.
 */
static inline void apic_eoi(void)
{
    apic_writel(0, APIC_EOI);
}

/*
 * Detect the presence of a local APIC.
 * See Intel SDM 12.4.2
 */
static inline bool apic_detect(void)
{
    return boolean(BIT_READ(cpuid_edx(CPUID_LEAF_GETFEATURES), 9));
}

void apic_disable(void)
{
    uint64_t msr;

    if (!cpu_has_msr())
        return;

    log_info("disabling local APIC");

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
    if (!apic_detect()) {
        log_info("CPU feature do not include APIC");
        return E_NOT_SUPPORTED;
    }

    /*
     * Same for MSRs, which are required for controlling the APIC.
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

    vm_free(&kernel_address_space, apic_regs);
    return E_NOT_IMPLEMENTED;
}
