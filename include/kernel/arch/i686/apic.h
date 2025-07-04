#ifndef KERNEL_ARCH_I686_APIC_H
#define KERNEL_ARCH_I686_APIC_H

#include <kernel/error.h>

/** Initialize the local APIC interrupt controller. */
error_t apic_init(void);

/** Software disable of the APIC interrupt controller.
 *  WARNING: Once the APIC has been disabled, it cannot be re-enabled until
 *           a soft-reset.
 */
void apic_disable(void);

#endif /* KERNEL_ARCH_I686_APIC_H */
