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

/** Return wether a custom interrupt has been installed for the given vector */
static inline bool interrupts_has_been_installed(u8 irq)
{
    return interrupts_get_handler(irq, NULL) != NULL;
}

/** Returns the name of an interrupt from its vector number */
const char *interrupts_to_str(u8 nr);

/** Compute the interrupt's handler's name */
#define INTERRUPT_HANDLER(_interrupt) _interrupt##_handler

/**
 * @brief Define an interrupt handler function for a given interrupt
 * You must always use this function when defining an interrupt handler.
 */
#define INTERRUPT_HANDLER_FUNCTION(_interrupt) \
    u32 INTERRUPT_HANDLER(_interrupt)(void *data)

/** @brief Disable interrupts on the current CPU. */
static inline void interrupts_disable(void)
{
    arch_interrupts_disable();
}

/** @brief Enable interrupts on the current CPU. */
static inline void interrupts_enable(void)
{
    arch_interrupts_enable();
}

/* @brief Disable CPU interrupts on the current CPU.
 * @return \c true if the interrupts where enabled previously.
 */
static inline bool interrupts_test_and_disable(void)
{
    return arch_interrupts_test_and_disable();
}

/** Restore the previous interrupt state.
 *
 *  @param enabled The previous interrupt state (on/off).
 */
static inline void interrupts_restore(bool enabled)
{
    if (enabled)
        interrupts_enable();
}

typedef struct {
    bool enabled;
    bool done;
} scope_irq_off_t;

static inline scope_irq_off_t scope_irq_off_constructor(void)
{
    return (scope_irq_off_t){
        .enabled = interrupts_test_and_disable(),
        .done = false,
    };
}

static inline void scope_irq_off_destructor(scope_irq_off_t *guard)
{
    interrupts_restore(guard->enabled);
}

/** Define a scope inside which irqs are disabled on the current CPU.
 *
 *  WARNING: As this macro uses a for loop to function, any 'break' directive
 *  placed inside it will break out of the guarded scope instead of that of its
 *  containing loop.
 */
#define interrupts_disabled_scope()                                \
    for (scope_irq_off_t guard CLEANUP(scope_irq_off_destructor) = \
             scope_irq_off_constructor();                          \
         !guard.done; guard.done = true)

/** @} */

#endif /* KERNEL_INTERRUPTS_H */
