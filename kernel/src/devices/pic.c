#include <kernel/devices/pic.h>
#include <kernel/devices/serial.h>

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
    // Save masks, overwritten when sending ICWs
    uint8_t mask_master = inb(PIC_DATA(PIC_MASTER));
    uint8_t mask_slave = inb(PIC_DATA(PIC_SLAVE));

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

    // Restore master
    outb(PIC_DATA(PIC_MASTER), mask_master);
    outb(PIC_DATA(PIC_SLAVE), mask_slave);
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
    const uint16_t pic = (irq >= PIC_SIZE) ? PIC_SLAVE : PIC_MASTER;
    const uint8_t mask = inb(PIC_DATA(pic));

    outb(PIC_DATA(pic), BIT_MASK(mask, irq % PIC_SIZE));
}

void pic_disable_irq(pic_irq irq)
{
    const uint16_t pic = (irq >= PIC_SIZE) ? PIC_SLAVE : PIC_MASTER;
    const uint8_t mask = inb(PIC_DATA(pic));

    outb(PIC_DATA(pic), BIT_SET(mask, irq % PIC_SIZE));
}
