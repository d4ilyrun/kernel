/**
 * @file kernel/console.h
 * @defgroup kernel_console Console
 * @ingroup kernel
 *
 * @{
 */
#ifndef KERNEL_CONSOLE_H
#define KERNEL_CONSOLE_H

#include <kernel/error.h>

#include <libalgo/linked_list.h>

enum console_color {
    COLOR_NONE,
    COLOR_BLACK,
    COLOR_BLUE,
    COLOR_GREEN,
    COLOR_CYAN,
    COLOR_RED,
    COLOR_MAGENTA,
    COLOR_YELLOW,
    COLOR_WHITE,
    COLOR_BOLD_RED,
    COLOR_BOLD_GREEN,
    COLOR_BOLD_YELLOW,
    COLOR_BOLD_BLUE,
    COLOR_BOLD_MAGENTA,
    COLOR_BOLD_CYAN,
    COLOR_BOLD_WHITE,
    COLOR_COUNT
};

/* Kernel console.
 *
 * There can be multiple consoles inside the kernel,
 * but only one can be active at a time. Once a console
 * has been registered with console_register() it can
 * be chosen as the active console using console_set_active().
 */
struct console {
    LLIST_NODE(this);
    const char *name;
    ssize_t (*write)(const struct console *, const char *buffer, size_t size);
    void (*set_color)(const struct console *, enum console_color fg,
                      enum console_color bg);
};

/** Register a console with the kernel.
 *
 * Registered consoles can later be selected as the active console
 * using console_set_active().
 */
error_t console_register(struct console *console);

/** Select the active console by name.
 *
 * All subsequent calls to console_write() will be routed
 * to the selected console's write callback.
 *
 * @param name Name of the console to activate.
 */
error_t console_set_active(const char *name);

/** Write data to the active console.
 *
 * @param buffer Buffer containing the data to write.
 * @param size Number of bytes to write.
 *
 * @return Number of bytes written on success, a negative error
 *         code on failure.
 */
ssize_t console_write(const char *buffer, size_t size);

#define console_write_string(str) console_write(str, strlen(str))

/*
 *
 */
void console_set_color(enum console_color fg, enum console_color bg);

static inline void console_set_fg_color(enum console_color color)
{
    return console_set_color(color, COLOR_NONE);
}


#endif /* KERNEL_CONSOLE_H */

/** @} */
