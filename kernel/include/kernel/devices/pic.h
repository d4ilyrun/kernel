/*
 * Interface with the 8259 PIC.
 *
 * Programmable Interrupt Controller.
 *
 * Any interaction done with the PIC should be done through
 * the functions defined inside this header.
 *
 * Reference manual can be found here:
 *  https://pdos.csail.mit.edu/6.828/2005/readings/hardware/8259A.pdf
 */

#ifndef KERNEL_DEVICES_PIC_H
#define KERNEL_DEVICES_PIC_H

#include <stdint.h>

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
void pic_eoi(uint8_t irq);

/* Disable the given IRQ */
void pic_disable_irq(uint8_t irq);

/* Enable the given IRQ */
void pic_enable_irq(uint8_t irq);

#endif /* end of include guard: KERNEL_DEVICES_PIC_H */
