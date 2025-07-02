/**
 * @brief x86 specific interrupt interface implementation
 *
 * @file kernel/arch/i686/interrupts.h
 *
 * @defgroup x86_interrupts Interrupts - x86 specific
 * @ingroup x86
 * @ingroup interrupts
 *
 * @{
 *
 * # x86 specific interrupt interface implentation
 *
 * ## Design
 *
 * On x86 the interrupts are set-up using an Interrupt Descriptor Table.
 *
 * This table can be located anywhere in memory and is pointed to by the address
 * inside the IDTR register. It contains a pointer to each interrupt's specific
 * handler, as well as information about how we want to run the interrupt
 * (should never change).
 *
 * Despite that, for conviniency, we define a common stub handler for each known
 * CPU interrupts, which pushes additional information about the interrupt, and
 * eventually calls the actual handler (if we defined one). The actual
 * interrupts are software defined, located inside a static array and can be
 * modified at will using @link interrupts_set_handler @endlink..
 *
 * This lets us have more control on how we handle the interrupts.
 *
 * @see Intel developper manual, section 6
 * @see @link https://wiki.osdev.org/Interrupt_Descriptor_Table OsDev - IDT
 */

#ifndef KERNEL_INTERRUPTS_H
#error <kernel/arch/i686/interrupts.h> must not be used as a standalone header. Please include <kernel/interrupts.h> instead.
#endif

#ifndef KERNEL_ARCH_I686_INTERRUPTS_H
#define KERNEL_ARCH_I686_INTERRUPTS_H

#include <kernel/types.h>

#include <kernel/arch/i686/gdt.h>

#include <utils/compiler.h>

#define INTERRUPTS_COUNT 256

/**
 * @enum x86_exceptions
 * @brief List of all x86 CPU exceptions
 * @ref Intel developper manual, Table 6-1
 */
typedef enum {
    DIVISION_ERROR = 0x0,
    DEBUG,
    NON_MASKABLE,
    BREAKPOINT,
    OVERFLOW,
    BOUND_RANGE_EXCEEDED,
    INVALID_OPCODE,
    DEVICE_NOT_AVAILABLE,
    DOUBLE_FAULT,
    COPROCESSOR_SEGMENT_OVERRUN,
    INVALID_TSS,
    SEGMENT_NOT_PRESENT,
    STACK_SEGMENT_FAULT,
    GENERAL_PROTECTION_FAULT,
    PAGE_FAULT,
    X87_FPE = 0x10,
    ALIGNMENT_CHECK,
    SIMD_FPE,
    VIRTUALIZATTION_EXCEPTION,
    CONTROL_PROTECTION_EXCEPTION,
    HYPERVISOR_INJECTION_EXCEPTION = 0x1C,
    VMM_COMMUNICATION_EXCEPTION,
    SECURITY_EXCEPTION,
} x86_exceptions;

/** @brief Frame passed onto the interrupt handlers by our stub handler */
struct interrupt_frame {

    /**
     * @brief Dump of the process's registers.
     * These are pushed by `pusha` inside our stub handler
     */
    struct registers_dump {
        u32 edi, esi, ebp, esp;
        u32 ebx, edx, ecx, eax;
    } stub;

    /** Interrupt number (pushed by our stub) */
    u32 nr;
    /** Error code for this exception (pushed by our stub)  */
    u32 error;

    /**
     * @brief Default x86 interrupt frame pushed by the cpu
     * @ref Intel developper manual, figure 6-4
     */
    struct cpu_interrupt_frame {
        u32 eip;
        u32 cs;
        u32 flags;
        u32 esp;
        u32 ss;
    } state;
};

#define INLINED_INTERRUPTS_DISABLE_ENABLE

/* @brief Disable CPU interrupts
 * @return Whether the interrupts where previously enabled
 */
static ALWAYS_INLINE bool interrupts_enabled(void)
{
    u32 eflags;
    ASM("pushf; popl %0" : "=r"(eflags) :: "memory");
    return boolean(eflags & 0x200); // flag: IF
}

/** @brief Disable CPU interrupts */
static ALWAYS_INLINE void interrupts_disable(void)
{
    ASM("cli");
}

/** @brief Enable CPU interrupts */
static ALWAYS_INLINE void interrupts_enable(void)
{
    ASM("sti");
}

/* @brief Disable CPU interrupts
 * @return Whether the interrupts where previously enabled
 */
static ALWAYS_INLINE bool interrupts_test_and_disable(void)
{
    u32 eflags;
    ASM("pushf; cli; popl %0" : "=r"(eflags)::"memory");
    return boolean(eflags & 0x200); // flag: IF
}

#endif /* KERNEL_I686_INTERRUPTS_H */
