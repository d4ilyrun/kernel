#ifndef _KERNEL_FRAMEBUFFER_H
#define _KERNEL_FRAMEBUFFER_H

#include <kernel/console.h>
#include <kernel/device.h>
#include <kernel/error.h>
#include <kernel/types.h>

#include <limits.h>

/*
 *
 */
struct framebuffer_params {
    unsigned int width;
    unsigned int height;
    unsigned int pitch;
    unsigned int bpp;
};

error_t framebuffer_register(paddr_t buffer, const struct framebuffer_params *);

#endif /* !_KERNEL_FRAMEBUFFER_H */
