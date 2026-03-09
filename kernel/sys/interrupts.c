#define LOG_DOMAIN "interrupt"

#include <kernel/error.h>
#include <kernel/init.h>
#include <kernel/interrupts.h>
#include <kernel/logger.h>

#include <libalgo/linked_list.h>

static struct interrupt_chip interrupt_root_chip;

/*
 *
 */
static error_t
interrupt_chip_interrupt_handle(const struct interrupt_chip *chip,
                                unsigned int nr)
{
    /*
     * Invalid interrupt number (???) or no custom handler installed.
     */
    if (nr >= chip->interrupt_count || !chip->interrupts[nr].handler)
        return E_NOENT;

    chip->interrupts[nr].handler(chip->interrupts[nr].data);

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
    return interrupt_chip_interrupt_handle(&interrupt_root_chip, nr);
}

/*
 *
 */
static error_t
interrupt_chip_install_handler(struct interrupt_chip *chip, unsigned int nr,
                               interrupt_handler_func_t handler, void *data)
{
    if (nr >= chip->interrupt_count)
        return -E_INVAL;

    chip->interrupts[nr].handler = handler;
    chip->interrupts[nr].data = data;

    return E_SUCCESS;
}

/*
 *
 */
static interrupt_handler_func_t
interrupt_chip_get_handler(const struct interrupt_chip *chip, unsigned int nr,
                           void **pdata)
{
    if (pdata)
        *pdata = NULL;

    if (nr >= chip->interrupt_count)
        return PTR_ERR(E_NOENT);

    if (pdata)
        *pdata = chip->interrupts[nr].data;

    return chip->interrupts[nr].handler;
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
error_t
interrupts_set_handler(unsigned int nr, interrupt_handler_func_t handler, void *data)
{
    return interrupt_chip_install_handler(&interrupt_root_chip, nr, handler,
                                          data);
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
