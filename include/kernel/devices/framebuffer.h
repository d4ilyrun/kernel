#ifndef _KERNEL_FRAMEBUFFER_H
#define _KERNEL_FRAMEBUFFER_H

#include <kernel/console.h>
#include <kernel/device.h>
#include <kernel/error.h>
#include <kernel/types.h>

#include <dailyrun/framebuffer.h>

#include <limits.h>

error_t framebuffer_register(paddr_t buffer, const struct fb_params *);

#endif /* !_KERNEL_FRAMEBUFFER_H */
