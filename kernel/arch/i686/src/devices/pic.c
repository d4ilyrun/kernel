#include <kernel/cpu.h>
#include <kernel/devices/timer.h>
#include <kernel/interrupts.h>
#include <kernel/logger.h>
#include <kernel/terminal.h>

#include <kernel/i686/devices/pic.h>

#include <utils/macro.h>
#include <utils/types.h>

/* Number of IRQs in a single PIC */
#define PIC_SIZE 8

/* Interrupt Controller Port I/O addresses */
#define PIC_MASTER 0x0020
#define PIC_SLAVE 0x00A0

#define PIC_COMMAND(_port) ((_port) + 0x0)
#define PIC_DATA(_port) ((_port) + 0x1)

/* PIC command codes */
#define PIC_CMD_INIT 0X11
#define PIC_CMD_EOI 0X20

#define PIC_ICW4_8086 0x01

/** INTERRUPT HANDLERS */

static DEFINE_INTERRUPT_HANDLER(irq_keyboard);

void pic_reset()
{
    // ICW1: Start init process
    outb(PIC_COMMAND(PIC_MASTER), PIC_CMD_INIT);
    outb(PIC_COMMAND(PIC_SLAVE), PIC_CMD_INIT);

    // ICW2: Set offset vectors
    outb(PIC_DATA(PIC_MASTER), PIC_MASTER_VECTOR);
    outb(PIC_DATA(PIC_SLAVE), PIC_SLAVE_VECTOR);

    // ICW3: Setup master/slave relation
    outb(PIC_DATA(PIC_MASTER), 4); // master has a slave on IR 2
    outb(PIC_DATA(PIC_SLAVE), 2);  // slave is connected through pin 2

    // ICW4: Specify 8086 environment
    outb(PIC_DATA(PIC_MASTER), PIC_ICW4_8086);
    outb(PIC_DATA(PIC_SLAVE), PIC_ICW4_8086);

    // Disable all interrupts
    outb(PIC_DATA(PIC_MASTER), 0xFF);
    outb(PIC_DATA(PIC_SLAVE), 0xFF);

    // Set and enable custom interrupts
    log_info("PIC", "Setting up custom IRQs handlers");
    pic_enable_irq(IRQ_KEYBOARD);
    interrupts_set_handler(PIC_MASTER_VECTOR + IRQ_KEYBOARD,
                           INTERRUPT_HANDLER(irq_keyboard));
}

void pic_eoi(pic_irq irq)
{
    /* Operating in cascade mode, must also inform the slave PIC */
    if (irq >= PIC_SIZE)
        outb(PIC_COMMAND(PIC_SLAVE), PIC_CMD_EOI);

    outb(PIC_COMMAND(PIC_MASTER), PIC_CMD_EOI);
}

void pic_enable_irq(pic_irq irq)
{
    const u16 pic = (irq >= PIC_SIZE) ? PIC_SLAVE : PIC_MASTER;
    const u8 mask = inb(PIC_DATA(pic));

    outb(PIC_DATA(pic), BIT_MASK(mask, irq % PIC_SIZE));
}

void pic_disable_irq(pic_irq irq)
{
    const u16 pic = (irq >= PIC_SIZE) ? PIC_SLAVE : PIC_MASTER;
    const u8 mask = inb(PIC_DATA(pic));

    outb(PIC_DATA(pic), BIT_SET(mask, irq % PIC_SIZE));
}

static DEFINE_INTERRUPT_HANDLER(irq_keyboard)
{
    UNUSED(frame);

    static const u8 ascii[128] = {
        0x0,  0x0,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-',  '=',
        0x0,  0x0,  'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[',  ']',
        '\n', 0x0,  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
        0x0,  '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0x0,  '*',
        0x0,  ' ',  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,  0x0,
        0x0,  '7',  '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0',  '.',
    };

    const u8 scan_code = inb(0x60);

    // If not key release;  write character
    if (!BIT_READ(scan_code, 7) && ascii[scan_code])
        tty_putchar(ascii[scan_code]);

    pic_eoi(IRQ_KEYBOARD);
}
