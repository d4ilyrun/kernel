/**
 * x86 specific interrupt interface.
 *
 * @ref https://wiki.osdev.org/Interrupt_Descriptor_Table IDT
 */

#ifndef KERNEL_I686_INTERRUPTS_H
#define KERNEL_I686_INTERRUPTS_H

#include <kernel/i686/gdt.h>

#include <utils/compiler.h>
#include <utils/types.h>

#define IDT_LENGTH 256
#define IDT_SIZE (IDT_LENGTH * sizeof(idt_descriptor))
#define IDT_BASE_ADDRESS 0x00000000UL

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

typedef enum idt_gate_type {
    TASK_GATE = 0x5,
    INTERRUPT_GATE = 0x6,
    TRAP_GATE = 0x7,
    INTERRUPT_GATE_32B = 0xE,
    TRAP_GATE_32B = 0xF,
} idt_gate_type;

/**
 * @struct IDT Register
 * The location of the IDT is kept in the IDTR (IDT register).
 */
typedef struct idtr idtr;
struct PACKED idtr {
    u16 size;
    u32 offset;
};

/**
 * @struct Single entry inside the IDT
 */
typedef struct idt_descriptor idt_descriptor;
struct PACKED idt_descriptor {
    u16 offset_low; ///< 16 LSB fo the 32-bit offset
    segment_selector segment;
    u8 _reserved;
    u8 access;
    u16 offset_high; ///< 16 MSB fo the 32-bit offset
};

/** Frame passed onto the interrupt handlers by GCC
 *
 * See Intel developper manual, figure 6-4
 */
struct interrupt_frame {

    // Pushed by our interrupts stubs
    u32 nr;
    u32 error;

    u32 eip;
    u32 cs;
    u32 flags;
    u32 esp;
    u32 ss;
};

/** Print the content of the IDT and IDTR */
void idt_log(void);

#endif /* KERNEL_I686_INTERRUPTS_H */
