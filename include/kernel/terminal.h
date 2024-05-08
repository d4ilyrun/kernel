#ifndef KERNEL_TERMINAL_H
#define KERNEL_TERMINAL_H

#include <utils/compiler.h>

#include <stddef.h>
#include <stdint.h>

/* Hardware text mode color constants. */
enum vga_color {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE = 1,
    VGA_COLOR_GREEN = 2,
    VGA_COLOR_CYAN = 3,
    VGA_COLOR_RED = 4,
    VGA_COLOR_MAGENTA = 5,
    VGA_COLOR_BROWN = 6,
    VGA_COLOR_LIGHT_GREY = 7,
    VGA_COLOR_DARK_GREY = 8,
    VGA_COLOR_LIGHT_BLUE = 9,
    VGA_COLOR_LIGHT_GREEN = 10,
    VGA_COLOR_LIGHT_CYAN = 11,
    VGA_COLOR_LIGHT_RED = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_LIGHT_BROWN = 14,
    VGA_COLOR_WHITE = 15,
};

void tty_init(void);
void tty_putchar(char c);
void tty_write(const char *buffer, size_t size);
void tty_puts(const char *buffer);

/**
 * Set the active terminal color.
 *
 * \param color The new color, as returned by \c vga_entry_color
 */
void tty_set_color(uint8_t color);

#endif /* KERNEL_TERMINAL_H */
