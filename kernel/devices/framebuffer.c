#define LOG_DOMAIN "framebuffer"

#include <kernel/devices/driver.h>
#include <kernel/devices/framebuffer.h>
#include <kernel/error.h>
#include <kernel/file.h>
#include <kernel/init.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/memory.h>
#include <kernel/atomic.h>
#include <kernel/types.h>
#include <kernel/vm.h>
#include <kernel/interrupts.h>

#include <fonts/terminal.h>

#include <string.h>
#include <limits.h>

/*
 * Framebuffer device.
 */
struct framebuffer {
    struct device dev;
    struct framebuffer_params params;
    char name[NAME_MAX];
    void *front_buffer;
    void *back_buffer;

    /* Used by the console driver */
    struct console console;
    spinlock_t     console_lock;
    unsigned int   console_cur_i;
    unsigned int   console_cur_j;
    unsigned int   console_fg_color;
    unsigned int   console_bg_color;
};

/* Count the number of framebuffer active on the system */
static atomic_t framebuffer_count = { 0 };

static const u32 fb_color_palette[COLOR_COUNT] = {
    [COLOR_NONE]          = 0x00000000,
    [COLOR_BLACK]         = 0x00000000,
    [COLOR_BLUE]          = 0xFF0000CC,
    [COLOR_GREEN]         = 0xFF00CC00,
    [COLOR_CYAN]          = 0xFF00CCCC,
    [COLOR_RED]           = 0xFFCC0000,
    [COLOR_MAGENTA]       = 0xFFCC00CC,
    [COLOR_YELLOW]        = 0xFFCCCC00,
    [COLOR_WHITE]         = 0xFFCCCCCC,

    [COLOR_BOLD_RED]      = 0xFFFF5555,
    [COLOR_BOLD_GREEN]    = 0xFF55FF55,
    [COLOR_BOLD_YELLOW]   = 0xFFFFFF55,
    [COLOR_BOLD_BLUE]     = 0xFF5555FF,
    [COLOR_BOLD_MAGENTA]  = 0xFFFF55FF,
    [COLOR_BOLD_CYAN]     = 0xFF55FFFF,
    [COLOR_BOLD_WHITE]    = 0xFFFFFFFF,
};

/*
 *
 */
static inline bool
fb_valid_pixel(const struct framebuffer *fb, unsigned int i, unsigned int j)
{
    return i < fb->params.height && j < fb->params.width;
}

/*
 *
 */
static inline off_t
fb_pixel_offset(const struct framebuffer *fb, unsigned int i, unsigned int j)
{
    return (i * fb->params.pitch) + (j * fb->params.bpp / 8);
}

/*
 *
 */
static inline size_t fb_size(const struct framebuffer *fb)
{
    return fb->params.height * fb->params.pitch;
}

/*
 *
 */
static void fb_put_pixel(const struct framebuffer *fb, unsigned int i,
                         unsigned int j, u32 rgba)
{
    off_t pixel_offset;

    if (!fb_valid_pixel(fb, i, j))
        return;

    pixel_offset = fb_pixel_offset(fb, i, j);
    switch (fb->params.bpp) {
    case 32:
        *(u32 *)(fb->back_buffer + pixel_offset) = rgba;
        *(u32 *)(fb->front_buffer + pixel_offset) = rgba;
        break;
    default:
        /* this case should be filtered out in framebuffer_register() */
        assert_not_reached();
        break;
    }
}

/*
 *
 */
static void fb_put_char(const struct framebuffer *fb, unsigned int i,
                        unsigned int j, unsigned char c)
{
    /* non-printable characters */
    if (c < 32 || c > 126)
        c = '?';

    for (unsigned int font_i = 0; font_i < TERMINAL_FONT_HEIGHT; ++font_i) {
        u8 row = terminal_font[c][font_i];
        for (unsigned int font_j = 0; font_j < 8; ++font_j) {
            u32 rgba = row & 0x80 ? fb->console_fg_color : fb->console_bg_color;
            fb_put_pixel(fb, i + font_i, j + font_j, rgba);
            row <<= 1;
        }
    }
}

/*
 *
 */
static void fb_console_scroll(struct framebuffer *fb, unsigned int height)
{
    u32 *front_buffer = fb->front_buffer;
    u32 *back_buffer = fb->back_buffer;
    size_t size;
    off_t offset;

    /* Move the current end of the buffer to the start.
     *
     * This operation is performed pixel-per-pixel to avoid re-drawing
     * pixels that did not change since accessing video memory is slow.
     */
    offset = fb_pixel_offset(fb, height, 0) / sizeof(u32);
    size = (fb->params.pitch * (fb->params.height - height)) / sizeof(u32);
    for (size_t i = 0; i < size ; i += 1) {
        if (back_buffer[i] != back_buffer[i + offset]) {
            back_buffer[i] = back_buffer[i + offset];
            front_buffer[i] = back_buffer[i];
        }
    }

    /* Clear the end of the framebuffer */
    offset = fb_pixel_offset(fb, fb->params.height - height, 0) / sizeof(u32);
    size = (fb->params.pitch * height) / sizeof(u32);
    for (size_t i = 0; i < size ; i += 1) {
        if (back_buffer[offset + i] != 0) {
            front_buffer[offset + i] = 0;
            back_buffer[offset + i] = 0;
        }
    }

    fb->console_cur_i -= height;
}

/*
 *
 */
static ssize_t framebuffer_console_write(const struct console *console,
                                         const char *buffer, size_t bytes)
{
    struct framebuffer *fb;
    bool interrupts;

    fb = container_of(console, struct framebuffer, console);

    interrupts = interrupts_test_and_disable();
    spinlock_acquire(&fb->console_lock);

    for (size_t i = 0; i < bytes; ++i) {
        unsigned char c = buffer[i];

        switch (c) {
        case '\n':
            fb->console_cur_i += TERMINAL_FONT_HEIGHT;
            fb->console_cur_j = 0;
            break;
        case '\r':
            fb->console_cur_j = 0;
            break;
        case '\t':
            c = ' ';
            fallthrough;
        default:
            fb_put_char(fb, fb->console_cur_i, fb->console_cur_j, c);
            fb->console_cur_j += TERMINAL_FONT_WIDTH;
            if (fb->console_cur_j >= fb->params.width) {
                fb->console_cur_i += TERMINAL_FONT_HEIGHT;
                fb->console_cur_j = 0;
            }
            break;
        }

        /* reached the end of the screen, scroll down a bit */
        if (fb->console_cur_i + TERMINAL_FONT_HEIGHT > fb->params.height)
            fb_console_scroll(fb, fb->params.height / 8);
    }

    spinlock_release(&fb->console_lock);
    if (interrupts)
            interrupts_enable();

    return 0;
}

/*
 *
 */
static void framebuffer_console_set_color(const struct console *console,
                                          enum console_color fg_color,
                                          enum console_color bg_color)
{
    struct framebuffer *fb;

    if (fg_color == COLOR_NONE || fg_color >= COLOR_COUNT)
        fg_color = COLOR_WHITE;
    if (bg_color >= COLOR_COUNT)
        bg_color = COLOR_NONE;

    fb = container_of(console, struct framebuffer, console);
    locked_scope(&fb->console_lock) {
        fb->console_fg_color = fb_color_palette[fg_color];
        fb->console_bg_color = fb_color_palette[bg_color];
    }
}

static const struct file_operations framebuffer_fops = {
};

static struct device_driver framebuffer_driver = {
    .name = "framebuffer"
};

/*
 *
 */
error_t
framebuffer_register(paddr_t buffer, const struct framebuffer_params *params)
{
    struct framebuffer *fb;
    struct device *dev;
    size_t bufsize;

    if (params->bpp != 32) {
        log_err("unsupported bits per pixel value: %dbpp", params->bpp);
        return E_INVAL;
    }

    fb = kcalloc(1, sizeof(*fb), KMALLOC_KERNEL);
    if (!fb)
        return E_NOMEM;

    bufsize = align_up(params->height * params->pitch, PAGE_SIZE);
    log_info("creating %dx%dx%d framebuffer @ %p (%zuKB)", params->width, params->height,
             params->bpp, (void *)buffer, bufsize / 1000);

    fb->back_buffer = vm_alloc_at(&kernel_address_space, buffer, bufsize,
                                  VM_KERNEL_RW | VM_CACHE_WC);
    if (!fb->back_buffer) {
        log_err("failed to allocate back buffer");
        kfree(fb);
        return E_NOMEM;
    }

    fb->front_buffer = vm_alloc(&kernel_address_space, bufsize, VM_KERNEL_RW);
    if (!fb->front_buffer) {
        log_err("failed to allocate front buffer");
        vm_free(&kernel_address_space, fb->back_buffer);
        kfree(fb);
        return E_NOMEM;
    }

    memcpy(&fb->params, params, sizeof(*params));
    snprintk(fb->name, sizeof(fb->name), "fb%d",
             atomic_inc(&framebuffer_count));

    dev = &fb->dev;
    dev->driver = &framebuffer_driver;
    dev->fops = &framebuffer_fops;
    device_register(dev);

    fb->console.name = fb->name;
    fb->console.write = framebuffer_console_write;
    fb->console.set_color = framebuffer_console_set_color;
    framebuffer_console_set_color(&fb->console, COLOR_WHITE, COLOR_NONE);
    console_register(&fb->console);

    return E_SUCCESS;
}

/*
 *
 */
static error_t framebuffer_init(void)
{
    driver_register(&framebuffer_driver);

    return E_SUCCESS;
}

DECLARE_INITCALL(INIT_NORMAL, framebuffer_init);
