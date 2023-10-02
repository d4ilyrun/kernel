#ifndef KERNEL_TERMINAL_H
#define KERNEL_TERMINAL_H

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

/**
 * Generate a valid vga color combination from the given forgeroung and
 * background.
 */
static inline uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg)
{
    return fg | bg << 4;
}

/**
 * Generate a valid vga entry, displaying a character with the given color
 * combo.
 */
static inline uint16_t vga_entry(unsigned char uc, uint8_t color)
{
    // NOLINTNEXTLINE(readability-magic-numbers)
    return (uint16_t)uc | (uint16_t)color << 8;
}

void tty_init(void);
void tty_putchar(char c);
void tty_write(const char *buffer, size_t size);
void tty_puts(const char *buffer);

#endif /* KERNEL_TERMINAL_H */
