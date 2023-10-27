#include <kernel/gdt.h>
#include <kernel/i686/interrupts.h>
#include <kernel/interrupts.h>
#include <kernel/logger.h>

#include <string.h>
#include <utils/compiler.h>

static volatile idt_descriptor *const IDT = IDT_BASE_ADDRESS;

void interrupts_disable(void)
{
    ASM("cli");
}

void interrupts_enable(void)
{
    ASM("sti");
}

static inline idt_descriptor interrupt(idt_gate_type type, u32 address)
{
    if (type == TASK_GATE) {
        return (idt_descriptor){
            .offset_low = 0,
            .segment = GDT_ENTRY_TSS,
            .access = TASK_GATE | 0x80,
            .offset_high = 0,
        };
    }

    return (idt_descriptor){
        .offset_low = address & 0xFFFF,
        .segment = GDT_ENTRY_KERNEL_CODE,
        .access = type | 0x80,
        .offset_high = address >> 16,
    };
}

static inline void interrupts_set(size_t nr, idt_gate_type type,
                                  interrupt_handler handler)
{
    if (nr >= IDT_LENGTH) {
        // TODO: Print index in log message
        log_err("IDT", "Invalid index");
        return;
    }

    idt_descriptor entry = interrupt(type, (u32)handler);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
    IDT[nr] = entry; // NOLINT
#pragma GCC diagnostic pop
}

void interrupts_init(void)
{
    interrupts_disable();

    // Load up the IDTR
    static volatile idtr idtr = {.size = IDT_SIZE - 1,
                                 .offset = IDT_BASE_ADDRESS};
    ASM("lidt (%0)" : : "m"(idtr) : "memory");

    // Empty descriptor slots in the IDT should have the present flag set to 0.
    // Fill the whole IDT with null descriptors
    memset(IDT_BASE_ADDRESS, 0, IDT_SIZE); // NOLINT

    // Setup all known interrupts
}
