#include <kernel/memory.h>
#include <kernel/terminal.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TTY_MAX_WIDTH 80
#define TTY_MAX_HEIGHT 80

// VGA text mode buffer is located at 0xB8000
#define TTY_BUFFER_START ((uint16_t *)0xB8000)

struct terminal_info {
    size_t row;
    size_t column;
    uint8_t color;    // The current color palette used by the tty
    uint16_t *buffer; // The tty's buffer
};

static struct terminal_info g_terminal;

#define INDEX(_x, _y) ((_y)*TTY_MAX_WIDTH + (_x))

/**
 * Generate a valid vga color combination from the given forgeroung and
 * background.
 */
static ALWAYS_INLINE uint8_t vga_entry_color(enum vga_color fg,
                                             enum vga_color bg)
{
    return fg | bg << 4;
}

/**
 * Generate a valid vga entry, displaying a character with the given color
 * combo.
 */
static ALWAYS_INLINE uint16_t vga_entry(unsigned char uc, uint8_t color)
{
    // NOLINTNEXTLINE(readability-magic-numbers)
    return (uint16_t)uc | (uint16_t)color << 8;
}

static ALWAYS_INLINE void tty_putchar_at(char c, size_t x, size_t y)
{
    size_t index = y * TTY_MAX_WIDTH + x;
    g_terminal.buffer[index] = vga_entry(c, g_terminal.color);
}

static ALWAYS_INLINE void tty_newline()
{
    // Fill the whole line with a ' ' character
    for (size_t x = g_terminal.column; x < TTY_MAX_WIDTH; ++x) {
        tty_putchar(' ');
    }
}

void tty_init(void)
{
    g_terminal = (struct terminal_info){
        .column = 0,
        .row = 0,
        .color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK),
        .buffer = (uint16_t *)KERNEL_HIGHER_HALF_VIRTUAL(TTY_BUFFER_START),
    };

    // Fill the whole buffer with a ' ' character, written grey on black.
    for (size_t y = 0; y < TTY_MAX_HEIGHT; ++y) {
        for (size_t x = 0; x < TTY_MAX_WIDTH; ++x) {
            tty_putchar_at(' ', x, y);
        }
    }
}

void tty_putchar(char c)
{
    if (c == '\n' || c == '\r') {
        tty_newline();
        return;
    }

    tty_putchar_at(c, g_terminal.column, g_terminal.row);

    // Advance 1 character, but stay within boudarie
    g_terminal.column += 1;
    g_terminal.row += g_terminal.column == TTY_MAX_WIDTH;
    g_terminal.column %= TTY_MAX_WIDTH;
    g_terminal.row %= TTY_MAX_HEIGHT;
}

void tty_write(const char *buffer, size_t size)
{
    for (size_t i = 0; i < size; ++i)
        tty_putchar(buffer[i]);
}

void tty_puts(const char *buffer)
{
    tty_write(buffer, strlen(buffer));
}

void tty_set_color(uint8_t color)
{
    g_terminal.color = color;
}
