#include "kernel/symbols.h"
#define LOG_DOMAIN "interrupt"

#include <kernel/devices/uart.h>
#include <kernel/init.h>
#include <kernel/interrupts.h>
#include <kernel/logger.h>
#include <kernel/printk.h>
#include <kernel/process.h>
#include <kernel/syscalls.h>

#include <utils/bits.h>
#include <utils/compiler.h>

#include <string.h>

struct interrupt {
    interrupt_handler handler; // The interrupt handler
    void *data;                // Data passed as an argument to the handler
};

/*
 * Custom ISRs, defined at runtime and called by __stub_interrupt_handler
 * using the interrupt number (if set).
 *
 * These are set using \c interrupts_set_handler().
 */
static struct interrupt interrupt_handlers[INTERRUPTS_COUNT];

void interrupts_set_handler(u8 nr, interrupt_handler handler, void *data)
{
    interrupt_handlers[nr].handler = handler;
    interrupt_handlers[nr].data = data;
}

interrupt_handler interrupts_get_handler(u8 irq, void **pdata)
{
    if (pdata != NULL)
        *pdata = interrupt_handlers[irq].data;

    return interrupt_handlers[irq].handler;
}

void interrupts_dump(void)
{
    struct interrupt *interrupt;
    const struct kernel_symbol *sym;
    const char *name;

    log_info("Interrupts:");
    for (size_t i = 0; i < INTERRUPTS_COUNT; ++i) {
        interrupt = &interrupt_handlers[i];
        sym = kernel_symbol_from_address((vaddr_t)interrupt->handler);
        name = interrupt_name(i);
        if (name)
            printk(" * [%s] %s(%p)", name, kernel_symbol_name(sym),
                   interrupt->data);
        else
            printk(" * [%ld] %s(%p)", i, kernel_symbol_name(sym),
                   interrupt->data);
    }
}

/*
 * Entrypoint to all interrupts.
 *
 * The lower level ISRs should:
 * 1. Construct the interrupt frame
 * 2. Call this default handler
 * 3. Restore previous state
 * 4. Return out of the interrupt
 */
void default_interrupt_handler(interrupt_frame frame)
{
    struct interrupt *interrupt = &interrupt_handlers[frame.nr];

    // Call the custom handler, defined inside custom_interrupt_handlers,
    // if it exists. Else, we consider this interrupt as 'unsupported'.

    if (interrupt->handler == NULL) {
        log_err("Unsupported interrupt: %s (" FMT32 ")",
                interrupt_name(frame.nr), frame.nr);
        log_dbg("Thread: '%s' (TID=%d)", current->process->name, current->tid);
        return;
    }

    current->frame = frame;
    thread_set_stack_pointer(current, (void *)frame.state.esp);

    // Pass the frame as argument if no data was given
    // This is done to not have to differiente CPU exceptions from custom IRQs
    interrupt->handler(interrupt->data ?: &frame);
}

extern error_t arch_interrupts_init(void);

static error_t interrupts_init(void)
{
    error_t err;

    WARN_ON_MSG(interrupts_enabled(), "Interrupts should be disabled during "
                                      "bootstrap.");

    err = arch_interrupts_init();
    if (err)
        PANIC("Failed to initialize interrupts: %s", err_to_str(err));

    /*
     * Empty the list of custom ISRs
     */
    memset(interrupt_handlers, 0, sizeof(interrupt_handlers));

    return E_SUCCESS;
}

DECLARE_INITCALL(INIT_BOOTSTRAP, interrupts_init);
