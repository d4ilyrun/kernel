#include <kernel/cpu.h>
#include <kernel/error.h>
#include <kernel/interrupts.h>
#include <kernel/logger.h>
#include <kernel/timer.h>
#include <kernel/types.h>

#include <kernel/arch/i686/devices/pic.h>

#include <utils/bits.h>
#include <utils/macro.h>

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

    pic_unmask_irq(IRQ_CASCADE); /* Allow IRQs on slave to be triggered */
}

void pic_eoi(pic_irq irq)
{
    /* Operating in cascade mode, must also inform the slave PIC */
    if (irq >= PIC_SIZE)
        outb(PIC_COMMAND(PIC_SLAVE), PIC_CMD_EOI);

    outb(PIC_COMMAND(PIC_MASTER), PIC_CMD_EOI);
}

void pic_unmask_irq(pic_irq irq)
{
    const u16 pic = (irq >= PIC_SIZE) ? PIC_SLAVE : PIC_MASTER;
    u8 mask = inb(PIC_DATA(pic));

    BIT_CLEAR(mask, irq % PIC_SIZE);
    outb(PIC_DATA(pic), mask);
}

void pic_mask_irq(pic_irq irq)
{
    const u16 pic = (irq >= PIC_SIZE) ? PIC_SLAVE : PIC_MASTER;
    u8 mask = inb(PIC_DATA(pic));

    BIT_SET(mask, irq % PIC_SIZE);
    outb(PIC_DATA(pic), mask);
}
