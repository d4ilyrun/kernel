#ifndef KERNEL_INTERRUPT_H
#define KERNEL_INTERRUPT_H

/**\brief Disable CPU interrupts */
void interrupt_disable(void);

/**\brief Enable CPU interrupts */
void interrupt_enable(void);

#endif /* KERNEL_INTERRUPT_H */
