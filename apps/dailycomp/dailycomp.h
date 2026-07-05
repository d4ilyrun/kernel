#ifndef _DAILYCOMP_H
#define _DAILYCOMP_H

#include <dailyrun/framebuffer.h>

#include <stdint.h>

struct input_device {
    struct context      *ctx;
    const char          *name;
    int                  fd;
    struct input_device *next;
};

struct context {
    struct input_device *input_devices;
    unsigned int         input_device_count;

    /* framebuffer context */
    int              fb_fd;
    struct fb_params fb_params;
    struct fb_buffer fb_back_buffer;

    /* cursor context */
    int32_t cursor_i;
    int32_t cursor_j;
    int32_t cursor_cur_i;
    int32_t cursor_cur_j;
};

#endif /* _DAILYCOMP_H */
