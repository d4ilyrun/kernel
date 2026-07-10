#ifndef _DAILYRUN_FRAMEBUFFER_H
#define _DAILYRUN_FRAMEBUFFER_H

#include <sys/ioctl.h>

#include <stddef.h>

enum fb_pixel_format {
    FB_PIXEL_RGBA,
};

/* Used for IO_FB_GET_PARAMS */
struct fb_params {
    unsigned int width;
    unsigned int height;
    unsigned int pitch;
    unsigned int bpp;
    enum fb_pixel_format pixel_format;
};

/* Used for IO_FB_GET_BUFFER */
struct fb_buffer {
    void   *back_buffer;
    size_t  back_buffer_size;
};

#endif /* _DAILYRUN_FRAMEBUFFER_H */
