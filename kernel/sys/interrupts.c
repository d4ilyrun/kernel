#define LOG_DOMAIN "interrupt"

#include <kernel/error.h>
#include <kernel/init.h>
#include <kernel/interrupts.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/sched.h>

#include <libalgo/linked_list.h>

static struct interrupt_chip interrupt_root_chip;

/*
 *
 */
static error_t
interrupt_chip_interrupt_handle(const struct interrupt_chip *chip,
                                unsigned int nr)
{
    struct interrupt_handler *handler;

    /*
     * Invalid interrupt number (???) or no custom handler installed.
     */
    if (nr >= chip->interrupt_count ||
        llist_is_empty(&chip->interrupts[nr].handlers))
        return E_NOENT;

    FOREACH_LLIST_ENTRY (handler, &chip->interrupts[nr].handlers, this) {
        if (handler->handler(handler->data) == INTERRUPT_HANDLED)
            break;
    }

    return E_SUCCESS;
}

/*
 * Entry point to all hardware IRQ handlers.
 *
 * Tries to find the appropriate IRQ handler (installed via
 * interrupts_set_handler()) and execute it.
 */
error_t interrupt_handle(unsigned int nr)
{
    error_t err;

    current->flags |= THREAD_RESCHED;

    err = interrupt_chip_interrupt_handle(&interrupt_root_chip, nr);

    if (!thread_is_kernel(current))
        thread_deliver_pending_signal(current);

    /*
     * Try to reschedule the current thread. This is the best time to do so.
     *
     * TODO: Make sure that a user thread does not return to userland with
     *       preemption disabled.
     */
    if (current->flags & THREAD_RESCHED)
        schedule();

    return err;
}

/*
 *
 */
static error_t
interrupt_chip_install_handler(struct interrupt_chip *chip, unsigned int nr,
                               struct interrupt_handler *handler)
{
    if (nr >= chip->interrupt_count)
        return -E_INVAL;

    llist_add_tail(&chip->interrupts[nr].handlers, &handler->this);

    return E_SUCCESS;
}

/*
 *
 */
static interrupt_handler_func_t
interrupt_chip_get_handler(const struct interrupt_chip *chip, unsigned int nr,
                           void **pdata)
{
    struct interrupt_handler *first;
    struct interrupt_vector *vector;

    if (pdata)
        *pdata = NULL;

    vector = &chip->interrupts[nr];
    if (nr >= chip->interrupt_count || llist_is_empty(&vector->handlers))
        return PTR_ERR(E_NOENT);

    first = llist_entry(llist_first(&vector->handlers), typeof(*first), this);
    if (pdata)
        *pdata = first->data;

    return first->handler;
}

/*
 *
 */
const char *interrupt_chip_interrupt_name(const struct interrupt_chip *chip,
                                          unsigned int nr)
{
    if (nr >= chip->interrupt_count)
        return "invalid";

    return chip->interrupts[nr].name ?: "unnamed";
}

/*
 *
 */
error_t interrupts_install_static_handler(unsigned int nr,
                                          struct interrupt_handler *handler)
{
    return interrupt_chip_install_handler(&interrupt_root_chip, nr, handler);
}

/*
 *
 */
error_t interrupts_install_handler(unsigned int nr,
                                   interrupt_handler_func_t handler, void *data)
{
    struct interrupt_chip *chip = &interrupt_root_chip;
    struct interrupt_handler *interrupt_handler;

    if (nr >= chip->interrupt_count)
        return E_INVAL;

    interrupt_handler = kmalloc(sizeof(*interrupt_handler), KMALLOC_KERNEL);
    if (!interrupt_handler)
        return E_NOMEM;

    interrupt_handler->handler = handler;
    interrupt_handler->data = data;

    return interrupt_chip_install_handler(chip, nr, interrupt_handler);
}

/*
 *
 */
interrupt_handler_func_t interrupts_get_handler(unsigned int nr, void **pdata)
{
    return interrupt_chip_get_handler(&interrupt_root_chip, nr, pdata);
}

/*
 *
 */
const char *interrupt_name(unsigned int nr)
{
    return interrupt_chip_interrupt_name(&interrupt_root_chip, nr);
}

/*
 *
 */
extern error_t arch_interrupts_init(struct interrupt_chip *chip);
static error_t interrupts_init(void)
{
    error_t err;

    if (interrupts_enabled())
        log_warn("interrupts enabled during bootstrap");

    err = arch_interrupts_init(&interrupt_root_chip);
    if (err)
        return err;

    return E_SUCCESS;
}

DECLARE_INITCALL(INIT_BOOTSTRAP, interrupts_init);
