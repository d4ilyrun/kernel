#ifndef KERNEL_INTERRUPTS_H
#define KERNEL_INTERRUPTS_H

#include <utils/types.h>

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

/** Frame passed onto the interrupt handlers by GCC */
typedef struct interrupt_frame interrupt_frame;

/** Function pointer to an interrupt handler */
typedef void (*interrupt_handler)(interrupt_frame);

/** Dynamically set an interrupt handler */
void interrupts_set_handler(u8, interrupt_handler);

/** Return the name of an interrupt from its vector number */
const char *interrupts_to_str(u8 nr);

/** Compute the interrupt's handler's name */
#define INTERRUPT_HANDLER(_interrupt) _interrupt##_handler

/**
 * \brief Define an interrupt handler function given a name
 *
 * You must always use this function when defining an interrupt handler.
 */
#define DEFINE_INTERRUPT_HANDLER(_interrupt) \
    void INTERRUPT_HANDLER(_interrupt)(struct interrupt_frame frame)

#endif /* KERNEL_INTERRUPTS_H */
