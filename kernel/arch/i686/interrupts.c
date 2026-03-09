#define LOG_DOMAIN "interrupt"

#include <kernel/interrupts.h>
#include <kernel/logger.h>
#include <kernel/process.h>
#include <kernel/syscalls.h>

#include <kernel/arch/i686/gdt.h>

#include <dailyrun/arch/i686/syscalls.h>

#define IDT_LENGTH 256
#define IDT_SIZE (IDT_LENGTH * sizeof(idt_descriptor))
#define IDT_BASE_ADDRESS 0x00000000UL

static volatile idt_descriptor idt[IDT_LENGTH];
static struct interrupt_vector idt_interrupt_vectors[IDT_LENGTH];

/* Global addressable interrupt handler stub tables (see interrupts.asm) */
extern interrupt_handler_func_t interrupt_handler_stubs[IDT_LENGTH];

/*
 * Protected mode Interrupts and Exceptions (Table 6-1, Intel vol.3)
 */
static const char *idt_interrupt_names[IDT_LENGTH] = {
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

/*
 * Entrypoint for hardware all hardware IRQs raised by the IDT.
 */
void arch_interrupt_handle(interrupt_frame frame)
{
    error_t err;

    thread_set_interrupt_frame(current, &frame);
    thread_set_stack_pointer(current, (void *)frame.state.esp);

    err = interrupt_handle(&frame, frame.nr);
    if (err == E_NOENT) {
        log_err("Unsupported interrupt: %s (" FMT32 ")",
                interrupt_name(frame.nr), frame.nr);
        log_dbg("Thread: '%s' (TID=%d)", current->process->name, current->tid);
        log_dbg("ERROR=" FMT32,                 frame.error);
        log_dbg("FLAGS=" FMT32,                 frame.state.flags);
        log_dbg("CS="    FMT32  ", SS="  FMT32, frame.state.cs, frame.state.ss);
        log_dbg("EIP="   FMT32  ", ESP=" FMT32, frame.state.eip, frame.state.esp);
    }
}

/*
 *
 */
static void configure_idt_entry(volatile idt_descriptor *desc,
                                idt_gate_type type, void *address)
{
    if (type == TASK_GATE) {
        *desc = (idt_descriptor){
            .offset_low = 0,
            // segment selector: TSS from GDT, level=0
            .segment.index = GDT_ENTRY_TSS,
            .access = TASK_GATE | IDT_PRESENT,
            .offset_high = 0,
        };
    } else {
        *desc = (idt_descriptor){
            .offset_low = (u32)address & 0xFFFF,
            // segment selector: kernel code from GDT, level=0
            .segment.index = GDT_ENTRY_KERNEL_CODE,
            .access = type | IDT_PRESENT,
            .offset_high = (u32)address >> 16,
        };
    }
}

/*
 * Called by interrupts_init() in sys/interrupts.c.
 *
 * Initialize the system's root interrupt chip.
 */
error_t arch_interrupts_init(struct interrupt_chip *root_chip)
{
    static volatile idtr idtr = {
        .size = IDT_SIZE - 1,
        .offset = (size_t)idt,
    };

    root_chip->interrupts = idt_interrupt_vectors;
    root_chip->interrupt_count = IDT_LENGTH;

    /*
     * Install every stub interrupt handlers (see interrupts.asm) and remove
     * every the custom interrupt vector.
     */
    for (int i = 0; i < IDT_LENGTH; ++i) {
        idt_interrupt_vectors[i].name = idt_interrupt_names[i];
        INIT_LLIST(idt_interrupt_vectors[i].handlers);
        configure_idt_entry(&idt[i], INTERRUPT_GATE_32B,
                            interrupt_handler_stubs[i]);
    }

    /*
     * Make syscall interrupt callable from userland.
     */
    idt[SYSCALL_INTERRUPT_NR].access |= (3 << 5);

    /*
     * IDT is fully configured, load it.
     */
    ASM("lidt (%0)" : : "m"(idtr) : "memory");

    return E_SUCCESS;
}
