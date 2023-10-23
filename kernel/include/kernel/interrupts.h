#ifndef KERNEL_INTERRUPTS_H
#define KERNEL_INTERRUPTS_H

/**\brief Disable CPU interrupts */
void interrupts_disable(void);

/**\brief Enable CPU interrupts */
void interrupts_enable(void);

/**
 * @brief Initialize interrupt related registers.
 *
 * @info This function must be called during the kernel setup phase.
 *
 * This is architecure dependant, as such it should
 * be implemented inside the `arch` directory.
 */
void interrupts_init(void);

#endif /* KERNEL_INTERRUPTS_H */
