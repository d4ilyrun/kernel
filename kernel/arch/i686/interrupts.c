#include <kernel/devices/pic.h>
#include <kernel/devices/uart.h>
#include <kernel/i686/gdt.h>
#include <kernel/i686/interrupts.h>
#include <kernel/interrupts.h>
#include <kernel/logger.h>

#include <string.h>
#include <utils/compiler.h>
#include <utils/macro.h>

static volatile idt_descriptor *const IDT = IDT_BASE_ADDRESS;

// IDT entry flag: gate is present
#define IDT_PRESENT 0x80

void interrupts_disable(void)
{
    ASM("cli");
}

void interrupts_enable(void)
{
    ASM("sti");
}

static ALWAYS_INLINE idt_descriptor idt_entry(idt_gate_type type, u32 address)
{
    if (type == TASK_GATE) {
        return (idt_descriptor){
            .offset_low = 0,
            // segment selector: TSS from GDT, level=0
            .segment = GDT_ENTRY_TSS << 3,
            .access = TASK_GATE | IDT_PRESENT,
            .offset_high = 0,
        };
    }

    return (idt_descriptor){
        .offset_low = address & 0xFFFF,
        // segment selector: kernel code from GDT, level=0
        .segment = GDT_ENTRY_KERNEL_CODE << 3,
        .access = type | IDT_PRESENT,
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

    idt_descriptor entry = idt_entry(type, (u32)handler);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
    IDT[nr] = entry; // NOLINT
#pragma GCC diagnostic pop
}

static DEFINE_INTERRUPT_HANDLER(division_error);
static DEFINE_INTERRUPT_HANDLER(invalid_opcode);
static DEFINE_INTERRUPT_HANDLER(general_protection);

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
    interrupts_set(0x0, TRAP_GATE_32B, INTERRUPT_HANDLER(division_error));
    interrupts_set(0x6, TRAP_GATE_32B, INTERRUPT_HANDLER(invalid_opcode));
    interrupts_set(0xD, TRAP_GATE_32B, INTERRUPT_HANDLER(general_protection));

    // Setup PIC IRQs
    interrupts_set(PIC_MASTER_VECTOR + IRQ_KEYBOARD, TRAP_GATE_32B,
                   INTERRUPT_HANDLER(irq_keyboard));
}

DEFINE_INTERRUPT_HANDLER(division_error)
{
    UNUSED(frame);
    log_info("trap", "division error");
}

DEFINE_INTERRUPT_HANDLER(invalid_opcode)
{
    UNUSED(frame);
    log_info("trap", "invalid_opcode");
}

DEFINE_INTERRUPT_HANDLER(general_protection)
{
    register u32 error_code;

    UNUSED(frame);
    log_info("trap", "general protection fault");

    ASM("pop %0" : "=r"(error_code)::);
}
