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

#include <kernel/error.h>
#include <kernel/types.h>

#include <libalgo/linked_list.h>

/**
 *  Values returned by an interrupt handler.
 */
typedef enum interrupt_return {
    INTERRUPT_HANDLED, /*!< Interrupt was handled by the handler. */
    INTERRUPT_IGNORED, /*!< Interrupt was for another handler. */
} interrupt_return_t;

/**
 *  @brief Frame passed onto the interrupt handlers when triggering an interrupt
 *  @note This is a only a forward declaration. The actual definition
 *        is done inside the arch-specific header.
 */
typedef struct interrupt_frame interrupt_frame;

/** Function pointer to an interrupt handler */
typedef interrupt_return_t (*interrupt_handler_func_t)(void *);

struct interrupt_handler {
    interrupt_handler_func_t    handler;
    void                        *data;
    node_t                      this; /* used by interrupt_vector->handlers */
};

/** A single hardware IRQ vector. */
struct interrupt_vector {
    const char  *name;
    llist_t     handlers;
};

struct interrupt_chip {
    struct interrupt_vector *interrupts; /* Array of struct interrupt_vector */
    size_t                  interrupt_count;
};

/** Dynamically set an interrupt handler
 *
 *  @param irq The IRQ number to associate the handler with
 *  @param handler The handler function called when the interrupt occurs
 *  @param data Data passed to the interrupt handler
 */
error_t
interrupts_install_handler(unsigned int irq, interrupt_handler_func_t, void *);

/** Install a pre-configured interrupt handler.
 *
 *  This function must be used only when configuring an interrupt handler
 *  at before the virtual memory subsystem is initialized (INIT_BOOTSTRAP).
 *
 *  The interrupt_handler strcuture is initialized and provided by the caller.
 */
error_t
interrupts_install_static_handler(unsigned int nr, struct interrupt_handler *);

/** Retreive the current handler for a given IRQ
 *
 * @param irq The interrupt number
 * @param[out] pdata If not NULL, the handler's associated data is stored inside
 *                   this pointer (optional)
 *
 * @note In the case of shared interrupts this function always returns the first
 *       installed handler.
 *
 * @return The current handler function fo the IRQ
 */
interrupt_handler_func_t interrupts_get_handler(unsigned int irq, void **);

error_t interrupt_handle(unsigned int nr);
const char *interrupt_name(unsigned int nr);

/** Compute the interrupt's handler's name */
#define INTERRUPT_HANDLER(_interrupt) _interrupt##_handler

/**
 * @brief Define an interrupt handler function for a given interrupt
 * You must always use this function when defining an interrupt handler.
 */
#define INTERRUPT_HANDLER_FUNCTION(_interrupt) \
    interrupt_return_t INTERRUPT_HANDLER(_interrupt)(void *data)

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

static inline bool interrupts_enabled(void)
{
    return arch_interrupts_enabled();
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
