#include <kernel/devices/uart.h>
#include <kernel/interrupts.h>
#include <kernel/logger.h>

#include <kernel/arch/i686/devices/pic.h>
#include <kernel/arch/i686/gdt.h>

#include <utils/bits.h>
#include <utils/compiler.h>

#include <string.h>

static volatile idt_descriptor idt[IDT_LENGTH];

// Global addressable interrupt handler stub tables
extern interrupt_handler interrupt_handler_stubs[IDT_LENGTH];

typedef struct {
    interrupt_handler handler; // The interrupt handler
    void *data;                // Data passed as an argument to the handler
} interrupt_handler_callback;

/*
 * Custom ISRs, defined at runtime and called by __stub_interrupt_handler
 * using the interrupt number (if set).
 *
 * These are set using \c interrupts_set_handler
 */
static interrupt_handler_callback custom_interrupt_handlers[IDT_LENGTH];

static const char *interrupt_names[] = {
    // Protected mode Interrupts and Exceptions (Table 6-1, Intel vol.3)
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Detected Overflow",
    "Out of Bounds",
    "Invalid Opcode",
    "No Math Coprocessor",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 FPU Error",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    // IRQs
    "IRQ_TIMER",
    "IRQ_KEYBOARD",
    "IRQ_CASCADE",
    "IRQ_COM2",
    "IRQ_COM1",
    "IRQ_LPT2",
    "IRQ_FLOPPY",
    "IRQ_LPT1",
    "IRQ_CMOS",
    "IRQ_FREE1",
    "IRQ_FREE2",
    "IRQ_FREE3",
    "IRQ_PS2",
    "IRQ_FPU",
    "IRQ_ATA_PRIMARY",
    "IRQ_ATA_SECONDARY",
};

// IDT entry flag: gate is present
#define IDT_PRESENT BIT(7)

const char *interrupts_to_str(u8 nr)
{
    static const char *unknown = "Unnamed Interrupt";

    if (nr < (sizeof interrupt_names / sizeof interrupt_names[0]))
        return interrupt_names[nr];

    return unknown;
}

void interrupts_set_handler(u8 nr, interrupt_handler handler, void *data)
{
    log_info("IDT", "Setting custom handler for '%s' (" LOG_FMT_8 ")",
             interrupts_to_str(nr), nr);
    custom_interrupt_handlers[nr].handler = handler;
    custom_interrupt_handlers[nr].data = data;
}

interrupt_handler interrupts_get_handler(u8 irq, void **pdata)
{
    if (pdata != NULL)
        *pdata = custom_interrupt_handlers[irq].data;
    return custom_interrupt_handlers[irq].handler;
}

static ALWAYS_INLINE idt_descriptor new_idt_entry(idt_gate_type type,
                                                  u32 address)
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

static inline void interrupts_set_idt(u16 nr, idt_gate_type type,
                                      interrupt_handler handler)
{
    if (nr >= IDT_LENGTH) {
        log_err("IDT", "interrupts_set: invalid index: " LOG_FMT_16, nr);
        return;
    }

    idt_descriptor entry = new_idt_entry(type, (u32)handler);

    idt[nr] = entry; // NOLINT
}

void interrupts_init(void)
{
    interrupts_disable();

    // Load up the IDTR
    static volatile idtr idtr = {.size = IDT_SIZE - 1, .offset = (size_t)idt};
    ASM("lidt (%0)" : : "m"(idtr) : "memory");

    // Empty descriptor slots in the IDT should have the present flag set to 0.
    // Fill the whole IDT with null descriptors
    memset((void *)idt, 0, IDT_SIZE);

    // Empty the list of custom ISRs
    memset(custom_interrupt_handlers, 0, sizeof(custom_interrupt_handlers));

    log_info("IDT", "Setting up interrupt handler stubs");
    for (int i = 0; i < IDT_LENGTH; ++i) {
        interrupts_set_idt(i, INTERRUPT_GATE_32B, interrupt_handler_stubs[i]);
    }

    log_dbg("IDT", "Finished setting up the IDT");
}

void idt_log(void)
{
    idtr idtr;
    ASM("sidt %0" : "=m"(idtr) : : "memory");
    log_info("IDT", "IDTR = { size: " LOG_FMT_16 ", offset:" LOG_FMT_32 " }",
             idtr.size, idtr.offset);

    log_info("IDT", "Interrupt descriptors");

    for (size_t i = 0; i < IDT_LENGTH; ++i) {
        idt_descriptor interrupt = idt[i];
        if (interrupt.segment.raw == 0)
            continue; // Uninitialized

        printf(LOG_FMT_8 " = { offset: " LOG_FMT_32 ", segment: " LOG_FMT_16
                         ", access: " LOG_FMT_8 " } <%s>\n",
               i, interrupt.offset_low | (interrupt.offset_high << 16),
               interrupt.segment, interrupt.access, interrupts_to_str(i));
    }
}

void default_interrupt_handler(interrupt_frame frame)
{
    interrupt_handler_callback *handler = &custom_interrupt_handlers[frame.nr];

    // Call the custom handler, defined inside custom_interrupt_handlers,
    // if it exists. Else, we consider this interrupt as 'unsupported'.

    if (handler->handler == NULL) {
        log_err("interrupt", "Unsupported interrupt: %s (" LOG_FMT_32 ")",
                interrupts_to_str(frame.nr), frame.nr);
        log_dbg("interrupt", "ERROR=" LOG_FMT_32, frame.error);
        log_dbg("interrupt", "FLAGS=" LOG_FMT_32, frame.state.flags);
        log_dbg("interrupt", "CS=" LOG_FMT_32 ", SS=" LOG_FMT_32,
                frame.state.cs, frame.state.ss);
        log_dbg("interrupt", "EIP=" LOG_FMT_32 ", ESP=" LOG_FMT_32,
                frame.state.eip, frame.state.esp);
        return;
    }

    // Pass the frame as argument if no data was given
    // This is done to not have to differiente CPU exceptions from custom IRQs
    handler->handler(handler->data ? handler->data : &frame);
}
