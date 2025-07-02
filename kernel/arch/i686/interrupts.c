#define LOG_DOMAIN "interrupt"

#include <kernel/init.h>
#include <kernel/interrupts.h>
#include <kernel/logger.h>
#include <kernel/syscalls.h>

#include <kernel/arch/i686/devices/pic.h>
#include <kernel/arch/i686/gdt.h>

#include <utils/bits.h>
#include <utils/compiler.h>
#include <utils/macro.h>

#include <string.h>

/**
 * @enum idt_gate_type
 * @brief The different types of interrupt gates
 * @ref Intel developper manual, section 6-11
 */
typedef enum idt_gate_type {
    TASK_GATE = 0x5,
    INTERRUPT_GATE = 0x6,
    TRAP_GATE = 0x7,
    INTERRUPT_GATE_32B = 0xE,
    TRAP_GATE_32B = 0xF,
} idt_gate_type;

/** @struct idtr IDT Register
 *  The location of the IDT is kept inside the IDTR (IDT register).
 */
struct PACKED idtr {
    /** Size of the IDT */
    u16 size;
    /** Linear address of the IDT  */
    u32 offset;
};

/** @struct idt_descriptor Single entry inside the IDT */
struct PACKED idt_descriptor {
    /** 16 lower bits of the handler function's address  */
    u16 offset_low;
    /** Selector for the segment inside which we want to run the handler */
    segment_selector segment;
    u8 _reserved;
    /** Acess restriction flags for this interrupt  */
    u8 access;
    /** 16 higher bits of the handler function's address  */
    u16 offset_high;
};

#define IDT_LENGTH INTERRUPTS_COUNT
#define IDT_SIZE (IDT_LENGTH * sizeof(struct idt_descriptor))

/* IDT entry access flags */
#define IDT_LEVEL(_ring) (((_ring) & 0x3) << 5)
#define RING_KERNEL 0
#define RING_USER 3
#define IDT_PRESENT BIT(7)

static volatile struct idt_descriptor idt[IDT_LENGTH];

// Global addressable interrupt handler stub tables
extern interrupt_handler interrupt_handler_stubs[IDT_LENGTH];

const char *interrupt_names[] = {
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
    [SYSCALL_INTERRUPT_NR] = "syscall",
};

static inline void
interrupts_set_idt_entry(u16 nr, idt_gate_type type, interrupt_handler handler)
{
    u32 address = (u32)handler;
    struct idt_descriptor desc;

    memset(&desc, 0, sizeof(desc));
    desc.access = type | IDT_PRESENT | IDT_LEVEL(RING_KERNEL);

    switch (type) {
    case TASK_GATE:
        desc.segment.index = GDT_ENTRY_TSS;
        break;
    default:
        // segment selector: kernel code from GDT; level=0
        desc.segment.index = GDT_ENTRY_KERNEL_CODE;
        desc.offset_low = address & 0xFFFF;
        desc.offset_high = address >> 16;
        break;
    }

    idt[nr] = desc;
    printk("%llX\n", *(u64*)&desc);
}

/*
 * Called from interrupts_init().
 */
error_t arch_interrupts_init(void)
{
    /*
     * Install our IDT.
     */
    static struct idtr idtr = {
        .size = IDT_SIZE - 1,
        .offset = (vaddr_t)idt,
    };
    ASM("lidt (%0)" : : "m"(idtr) : "memory");

    /*
     * Fill IDT with interrupt handler stubs (trampoline).
     *
     * Those trampoline ISRs are responsible for filling the interrupt frame
     * before jumping to default_interrupt_handler().
     */
    for (int i = 0; i < IDT_LENGTH; ++i)
        interrupts_set_idt_entry(i, INTERRUPT_GATE_32B,
                                 interrupt_handler_stubs[i]);

    /*
     * Mark syscall interrupt as callable from userland.
     */
    idt[SYSCALL_INTERRUPT_NR].access &= ~IDT_LEVEL(ALL_ONES);
    idt[SYSCALL_INTERRUPT_NR].access |= IDT_LEVEL(RING_USER);

    return E_SUCCESS;
}
