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

// Global addressable interrupt handler stub tables
extern interrupt_handler interrupt_handler_stubs[];
extern interrupt_handler pic_interrupt_handler_stubs[PIC_IRQ_COUNT];

/*
 * Custom ISRs, defined at runtime and called by __stub_interrupt_handler
 * using the interrupt number (if set).
 *
 * These are set using \c interrupts_set_handler
 */
static interrupt_handler custom_interrupt_handlers[IDT_LENGTH];

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

void interrupts_set_handler(u8 nr, interrupt_handler handler)
{
    log_info("IDT", "Setting custom handler for: " LOG_FMT_8, nr);
    custom_interrupt_handlers[nr] = handler;
}

static ALWAYS_INLINE idt_descriptor idt_entry(idt_gate_type type, u32 address)
{
    if (type == TASK_GATE) {
        return (idt_descriptor){
            .offset_low = 0,
            // segment selector: TSS from GDT, level=0
            .segment.index = GDT_ENTRY_TSS,
            .access = TASK_GATE | IDT_PRESENT,
            .offset_high = 0,
        };
    }

    return (idt_descriptor){
        .offset_low = address & 0xFFFF,
        // segment selector: kernel code from GDT, level=0
        .segment.index = GDT_ENTRY_KERNEL_CODE,
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

    // Empty the list of custom ISRs
    memset(custom_interrupt_handlers, 0, sizeof(custom_interrupt_handlers));

    // Setup all known interrupts
    log_info("IDT", "Setting up interrupt handler stubs");
    for (u8 i = 0; i <= 21; ++i)
        interrupts_set(i, INTERRUPT_GATE_32B, interrupt_handler_stubs[i]);

    log_info("IDT", "Setting up IRQ handler stubs");
    for (pic_irq irq = IRQ_TIMER; irq <= IRQ_ATA_SECONDARY; ++irq)
        interrupts_set(PIC_MASTER_VECTOR + irq, INTERRUPT_GATE_32B,
                       pic_interrupt_handler_stubs[irq]);

    log_dbg("IDT", "Finished setting up the IDT");
}

void idt_log(void)
{
    idtr idtr;
    ASM("sidt %0" : "=m"(idtr) : : "memory");
    log_info("IDT", "IDTR = { size: " LOG_FMT_16 ", offset:" LOG_FMT_32 " }",
             idtr.size, idtr.offset);

    log_info("IDT", "Interrupt descriptors");
    idt_descriptor *idt = (idt_descriptor *)idtr.offset;

    for (size_t i = 0; i < IDT_LENGTH; ++i) {
        idt_descriptor interrupt = idt[i];
        if (interrupt.segment.raw == 0)
            continue; // Uninitialized

        printf(LOG_FMT_8 " = { offset: " LOG_FMT_32 ", segment: " LOG_FMT_16
                         ", access: " LOG_FMT_8 " }\n",
               i, interrupt.offset_low | (interrupt.offset_high << 16),
               interrupt.segment, interrupt.access);
    }
}

DEFINE_INTERRUPT_HANDLER(default_interrupt)
{
    // Call the custom handler, defined inside custom_interrupt_handlers,
    // if it exists. Else, we consider this interrupt as 'unsupported'.

    if (custom_interrupt_handlers[frame.nr] == 0) {
        log_err("interrupt", "Unsupported interrupt: " LOG_FMT_32, frame.nr);
        log_dbg("interrupt", "ERROR=" LOG_FMT_32, frame.error);
        log_dbg("interrupt", "FLAGS=" LOG_FMT_32, frame.flags);
        log_dbg("interrupt", "CS=" LOG_FMT_32 ", SS=" LOG_FMT_32, frame.cs,
                frame.ss);
        log_dbg("interrupt", "EIP=" LOG_FMT_32 ", ESP=" LOG_FMT_32, frame.eip,
                frame.esp);
        return;
    }

    custom_interrupt_handlers[frame.nr](frame);
}
