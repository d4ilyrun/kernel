#include <kernel/atomic.h>
#include <kernel/console.h>
#include <kernel/device.h>
#include <kernel/file.h>
#include <kernel/init.h>
#include <kernel/process.h>
#include <kernel/vfs.h>
#include <kernel/waitqueue.h>

#include <libalgo/ringbuffer.h>

#include <string.h>

static const struct console *active_console = NULL;

static DECLARE_LLIST(consoles);
static DECLARE_SPINLOCK(consoles_lock);

static DECLARE_SPINLOCK(console_input_lock);
static char __console_input_buffer[4 * PAGE_SIZE];
static struct ringbuffer console_input_buffer;
static DECLARE_WAITQUEUE(console_input_waiters);

/*
 *
 */
error_t console_register(struct console *console)
{
	if (!console)
		return E_INVAL;

	locked_scope (&consoles_lock)
		llist_add(&consoles, &console->this);

	return E_SUCCESS;
}

/*
 *
 */
error_t console_set_active(const char *name)
{
	struct console *console;
	bool found = false;

	locked_scope (&consoles_lock) {
		FOREACH_LLIST_ENTRY (console, &consoles, this) {
			if (!strcmp(console->name, name)) {
				found = true;
				break;
			}
		}
	}

	if (!found)
		return E_NODEV;

	WRITE_ONCE(active_console, console);

	return E_SUCCESS;
}

/*
 *
 */
void console_set_color(enum console_color fg, enum console_color bg)
{
	const struct console *console;

	console = READ_ONCE(active_console);
	if (console->set_color)
		console->set_color(console, fg, bg);
}

/*
 *
 */
ssize_t console_write(const char *buf, size_t count)
{
	const struct console *console;

	/* sync with console_set_active(). */
	console = READ_ONCE(active_console);
	if (!console)
		return -E_NODEV;

	if (!console->write)
		return count;

	return console->write(console, buf, count);
}

/*
 *
 */
static ssize_t console_read(const char *buf, size_t count, bool nonblock)
{
	if (count == 0)
		return 0;

again:
	spinlock_acquire(&console_input_lock);
	if (ringbuffer_is_empty(&console_input_buffer)) {
		if (nonblock) {
			count = -E_WOULD_BLOCK;
			goto out;
		}

		waitqueue_lock(&console_input_waiters);
		spinlock_release(&console_input_lock);
		waitqueue_enqueue_locked(&console_input_waiters, current);
		goto again;
	}

	count = MIN(count, ringbuffer_remaining(&console_input_buffer));
	ringbuffer_pop(&console_input_buffer, (u8 *)buf, count);

out:
	spinlock_release(&console_input_lock);
	return count;
}

/*
 *
 */
void console_input(const char *buf, size_t len)
{
	if (len == 0)
		return;

	/* NOTE: for debugging */
	console_write(buf, len);

	spinlock_acquire(&console_input_lock);
	ringbuffer_push(&console_input_buffer, (const u8 *)buf,
			MIN(len, ringbuffer_available(&console_input_buffer)));
	spinlock_release(&console_input_lock);
	waitqueue_dequeue(&console_input_waiters);
}

/*
 * Write to the console device.
 */
static ssize_t console_device_write(struct file *file, const char *buf, size_t size)
{
	UNUSED(file);
	return console_write(buf, size);
}

/*
 * Read from the console device.
 */
static ssize_t console_device_read(struct file *file, char *buf, size_t size)
{
	return console_read(buf, size, file->flags & O_NONBLOCK);
}

static const struct file_operations console_device_fops = {
    .read = console_device_read,
    .write = console_device_write,
};

static struct device console_device = {
    .name = "console",
    .driver = NULL,
    .fops = &console_device_fops,
};

/*
 * Initialize console input ringbuffer.
 */
static error_t console_init(void)
{
	ringbuffer_init(&console_input_buffer, __console_input_buffer,
			sizeof(__console_input_buffer));

	return E_SUCCESS;
}
DECLARE_INITCALL(INIT_EARLY, console_init);

/*
 * Configure the default standard file descriptors to write to
 * the kernel's console. This should be called before starting
 * the init process.
 */
static error_t console_device_init(void)
{
	struct file *console;
	error_t err;

	err = device_register(&console_device);
	if (err)
		return err;

	console = vfs_open("/dev/console", O_RDWR);
	if (IS_ERR(console))
		PANIC("failed to open /dev/console: %pe", console);

	ASSERT(process_set_fd(&kernel_process, FD_STDIN,  file_get(console), 0) >= 0);
	ASSERT(process_set_fd(&kernel_process, FD_STDOUT, file_get(console), 0) >= 0);
	ASSERT(process_set_fd(&kernel_process, FD_STDERR, file_get(console), 0) >= 0);

	file_put(console);

	return E_SUCCESS;
}

DECLARE_INITCALL(INIT_LATE, console_device_init);
