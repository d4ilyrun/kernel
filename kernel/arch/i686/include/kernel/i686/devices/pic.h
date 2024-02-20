/*
 * Interface with the 8259 PIC.
 *
 * Programmable Interrupt Controller.
 *
 * Any interaction done with the PIC should be done through
 * the functions defined inside this header.
 *
 * This also includes the defition of the interrupt handlers for the IRQs
 * raised by the PIC.
 *
 * Reference manual can be found here:
 *  https://pdos.csail.mit.edu/6.828/2005/readings/hardware/8259A.pdf
 */

#ifndef KERNEL_ARCH_I686_DEVICES_PIC_H
#define KERNEL_ARCH_I686_DEVICES_PIC_H

#include <stdint.h>

/* Offset vectors
 *
 * They are set to 0x20 and 0x28 to not conflict with CPU exceptions
 * in protected mode.
 */
#define PIC_MASTER_VECTOR 0x20
#define PIC_SLAVE_VECTOR 0x28

/**
 * All available PIC irqs, by vector index
 */
typedef enum pic_irq {
    IRQ_TIMER = 0,
    IRQ_KEYBOARD,
    IRQ_CASCADE,
    IRQ_COM2,
    IRQ_COM1,
    IRQ_LPT2,
    IRQ_FLOPPY,
    IRQ_LPT1,
    IRQ_CMOS,
    IRQ_FREE1,
    IRQ_FREE2,
    IRQ_FREE3,
    IRQ_PS2,
    IRQ_FPU,
    IRQ_ATA_PRIMARY,
    IRQ_ATA_SECONDARY
} pic_irq;

#define PIC_IRQ_COUNT (IRQ_ATA_SECONDARY + 1)

/** Reset the PIC.
 *
 * Should be called when entering protected mode.
 */
void pic_reset();

/** Send an End Of Interrupt command to the PIC.
 *
 * This is issued to the PIC chips at the end of an IRQ-based interrupt routine
 *
 * @param interrupt The interrupt request we finished treating
 */
void pic_eoi(pic_irq);

/* Disable the given IRQ */
void pic_disable_irq(pic_irq);

/* Enable the given IRQ */
void pic_enable_irq(pic_irq);

#endif /* KERNEL_ARCH_I686_DEVICES_PIC_H */
