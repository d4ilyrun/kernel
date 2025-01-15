#include <kernel/console.h>
#include <kernel/file.h>

static const struct early_console *active_early_console = NULL;

static struct console active_console = {
    .out = NULL,
};

error_t console_early_setup(struct early_console *console, void *pdata)
{
    error_t ret;

    if (!console->write)
        return E_INVAL;

    if (console->init) {
        ret = console->init(pdata);
        if (ret != E_SUCCESS)
            return ret;
    }

    console->private = pdata;
    active_early_console = console;

    return E_SUCCESS;
}

error_t console_open(struct device *dev)
{
    struct file *dev_file = device_open(dev);
    if (IS_ERR(dev_file))
        return ERR_FROM_PTR(dev_file);

    active_console.out = dev_file;

    return E_SUCCESS;
}

error_t console_write(const char *buf, size_t count)
{
    if (!active_console.out && !active_early_console)
        return E_NODEV;

    if (!active_console.out) {
        return active_early_console->write(buf, count,
                                           active_early_console->private);
    }

    return active_console.out->ops->write(active_console.out, buf, count);
}
