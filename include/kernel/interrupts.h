/**
 * @file kernel/interrupts.h
 *
 * @defgroup interrupts Interrputs
 * @ingroup kernel
 *
 * # Interrupts
 *
 * Here are defined the interfaces used to setup and control interrupts.
 *
 * As interrupts are an architecure-dependent mechanism, the actual definitions
 * for the strucutre are present inside the 'arch' folder. This file only serves
 * as a common entry point for arch-generic code that would need to interact
 * with interrupts.
 *
 * @warning You should **never** include the arch specific headers directly
 *
 * @{
 */

#ifndef KERNEL_INTERRUPTS_H
#define KERNEL_INTERRUPTS_H

#if ARCH == i686
#include <kernel/arch/i686/interrupts.h>
#endif

#include <utils/types.h>

#ifndef INLINED_INTERRUPTS_DISABLE_ENABLE

/**@brief Disable CPU interrupts */
void interrupts_disable(void);

/**@brief Enable CPU interrupts */
void interrupts_enable(void);

#endif

/**
 * @brief Initialize interrupt related registers.
 *
 * @note This function must be called during the kernel setup phase.
 *
 * @note This is architecure dependant, as such it should be implemented
 *       inside the `arch` directory.
 */
void interrupts_init(void);

/**
 *  @brief Frame passed onto the interrupt handlers when triggering an interrupt
 *  @note This is a only a forward declaration. The actual definition
 *        is done inside the arch-specific header.
 */
typedef struct interrupt_frame interrupt_frame;

/** Function pointer to an interrupt handler */
typedef void (*interrupt_handler)(interrupt_frame);

/** Dynamically set an interrupt handler */
void interrupts_set_handler(u8, interrupt_handler);

/** Returns the name of an interrupt from its vector number */
const char *interrupts_to_str(u8 nr);

/** Compute the interrupt's handler's name */
#define INTERRUPT_HANDLER(_interrupt) _interrupt##_handler

/**
 * @brief Define an interrupt handler function for a given interrupt
 * You must always use this function when defining an interrupt handler.
 */
#define DEFINE_INTERRUPT_HANDLER(_interrupt) \
    void INTERRUPT_HANDLER(_interrupt)(struct interrupt_frame frame)

/** @} */

#endif /* KERNEL_INTERRUPTS_H */
