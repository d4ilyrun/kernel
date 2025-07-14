#include <kernel/console.h>
#include <kernel/file.h>
#include <kernel/init.h>
#include <kernel/process.h>
#include <kernel/vfs.h>

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

ssize_t console_write(const char *buf, size_t count)
{
    if (!active_console.out && !active_early_console)
        return E_NODEV;

    if (!active_console.out) {
        return active_early_console->write(buf, count,
                                           active_early_console->private);
    }

    return active_console.out->ops->write(active_console.out, buf, count);
}

static ssize_t
console_device_write(struct file *file, const char *buf, size_t size)
{
    UNUSED(file);
    return console_write(buf, size);
}

static const struct file_operations console_device_fops = {
    .write = console_device_write,
};

static struct device console_device = {
    .name = "console",
    .driver = NULL,
    .fops = &console_device_fops,
};

static error_t console_device_init(void)
{
    struct file *console;
    error_t err;

    err = device_register(&console_device);
    if (err)
        return err;

    console = vfs_open("/dev/console");
    if (IS_ERR(console))
        return ERR_FROM_PTR(console);

    kernel_process.files[FD_STDIN] = console;
    kernel_process.files[FD_STDOUT] = file_get(kernel_process.files[FD_STDIN]);
    kernel_process.files[FD_STDERR] = file_get(kernel_process.files[FD_STDOUT]);

    return E_SUCCESS;
}

DECLARE_INITCALL(INIT_LATE, console_device_init);
