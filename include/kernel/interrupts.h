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

#include <kernel/types.h>

#ifndef INLINED_INTERRUPTS_DISABLE_ENABLE

/** @brief Disable CPU interrupts */
void interrupts_disable(void);

/* @brief Disable CPU interrupts
 * @return Whether the interrupts where previously enabled
 */
bool interrupts_test_and_disable(void);

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
typedef u32 (*interrupt_handler)(void *);

/** Dynamically set an interrupt handler
 *
 *  @param irq The IRQ number to associate the handler with
 *  @param handler The handler function called when the interrupt occurs
 *  @param data Data passed to the interrupt handler
 *
 *  @info If \c data is NULL, the kernel will pass a pointer to the interrupt
 *        frame (\ref interrup_frame) when calling the handler instead
 */
void interrupts_set_handler(u8 irq, interrupt_handler, void *);

/** Retreive the current handler for a given IRQ
 *
 * @param irq The interrupt number
 * @param[out] pdata If not NULL, the handler's associated data is stored inside
 *                   this pointer (optional)
 *
 * @return The current handler function fo the IRQ
 */
interrupt_handler interrupts_get_handler(u8 irq, void **);

/** Returns the name of an interrupt from its vector number */
const char *interrupts_to_str(u8 nr);

/** Compute the interrupt's handler's name */
#define INTERRUPT_HANDLER(_interrupt) _interrupt##_handler

/**
 * @brief Define an interrupt handler function for a given interrupt
 * You must always use this function when defining an interrupt handler.
 */
#define DEFINE_INTERRUPT_HANDLER(_interrupt) \
    u32 INTERRUPT_HANDLER(_interrupt)(void *data)

/** @} */

#endif /* KERNEL_INTERRUPTS_H */
