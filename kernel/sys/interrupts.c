#define LOG_DOMAIN "interrupt"

#include <kernel/error.h>
#include <kernel/init.h>
#include <kernel/interrupts.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/sched.h>
#include <kernel/signal.h>
#include <kernel/worker.h>

#include <libalgo/linked_list.h>

static struct interrupt_chip interrupt_root_chip;

/*
 *
 */
static void threaded_interrupt(void *cookie)
{
    struct interrupt_handler *handler = cookie;
    struct interrupt_chip *chip = handler->chip;

    while (true) {
        /* The thread could well have been re-scheduled by the hw handler
         * since we unmask the irq before blocking it. */
        if (!atomic_read(&handler->thread_scheduled))
            sched_block_thread(current);
        atomic_write(&handler->thread_scheduled, false);

        handler->threaded_handler(handler->data);
        chip->irq_unmask(chip, handler->irq);
    }
}

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

    chip->irq_mask(chip, nr);

    FOREACH_LLIST_ENTRY (handler, &chip->interrupts[nr].handlers, this) {
        switch (handler->handler(handler->data)) {
        case INTERRUPT_IGNORED:
            continue;
        case INTERRUPT_HANDLED:
            if (chip->irq_eoi)
                chip->irq_eoi(chip, nr);
            chip->irq_unmask(chip, nr);
            goto end;
        case INTERRUPT_THREADED:
            if (chip->irq_eoi)
                chip->irq_eoi(chip, handler->irq);
            if (handler->thread) {
                atomic_write(&handler->thread_scheduled, true);
                sched_unblock_thread(handler->thread);
            } else {
                log_warn("%pS: no thread to wake up", handler->handler);
                chip->irq_unmask(chip, nr);
            }
            goto end;
        }
    }

end:
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

    handler->chip = chip;
    handler->irq = nr;

    if (handler->thread) {
        atomic_write(&handler->thread_scheduled, false);
        sched_new_thread(handler->thread);
    }

    llist_add_tail(&chip->interrupts[nr].handlers, &handler->this);
    if (chip->irq_unmask)
        chip->irq_unmask(chip, nr);

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
error_t interrupts_install_threaded_handler(unsigned int nr,
                                            interrupt_handler_func_t handler,
                                            interrupt_handler_func_t threaded,
                                            void *data)
{
    struct interrupt_chip *chip = &interrupt_root_chip;
    struct interrupt_handler *interrupt_handler;
    struct thread *thread = NULL;

    if (nr >= chip->interrupt_count)
        return E_INVAL;
    if (!handler)
        return E_INVAL;

    interrupt_handler = kmalloc(sizeof(*interrupt_handler), KMALLOC_KERNEL);
    if (!interrupt_handler)
        return E_NOMEM;

    if (threaded) {
        thread = thread_spawn(&kernel_process, threaded_interrupt,
                              interrupt_handler, NULL, NULL, THREAD_KERNEL);
        if (thread == NULL) {
            log_err("failed to interrupt thread");
            kfree(interrupt_handler);
            return E_NOMEM;
        }
    }

    interrupt_handler->handler = handler;
    interrupt_handler->threaded_handler = threaded;
    interrupt_handler->thread = thread;
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
 * Define a simple default interrupt handler that signals the process
 * through the appropriate UNIX signal vector.
 */
#define define_process_signal_interrupt_handler(_name, _signo, _code) \
    static u32 _name##_handler(MAYBE_UNUSED void *data)               \
    {                                                                 \
        siginfo_t info;                                               \
                                                                      \
        if (thread_is_kernel(current))                                \
            PANIC(stringify(_name));                                  \
                                                                      \
        info.si_signo = _signo;                                       \
        info.si_code = _code;                                         \
        signal_process(current->process, &info);                      \
                                                                      \
        return INTERRUPT_HANDLED;                                     \
    }                                                                 \
                                                                      \
    struct interrupt_handler _name = {                                \
        .handler = _name##_handler,                                   \
    }

define_process_signal_interrupt_handler(division_by_zero, SIGILL, 0);
define_process_signal_interrupt_handler(invalid_instruction, SIGILL, 0);

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
