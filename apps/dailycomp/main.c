#include <dailyrun/framebuffer.h>
#include <dailyrun/input.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libinput/libinput.h>

#include "dailycomp.h"

#define for_each_input_device(dev, ctx) \
    for (dev = (ctx)->input_devices; dev; dev = dev->next)

static inline bool valid_pixel(const struct context *ctx, int i, int j)
{
    if (j >= ctx->fb_params.width || i >= ctx->fb_params.height)
        return false;
    if (j < 0 || i < 0)
        return false;

    return true;
}

/*
 *
 */
static void put_pixel(struct context *ctx, int i, int j, uint32_t color)
{
    uint32_t *pixel;

    if (!valid_pixel(ctx, i, j))
        return;

    pixel = ctx->fb_back_buffer.back_buffer
            + i * ctx->fb_params.pitch
            + j * (ctx->fb_params.bpp / 8);

    *pixel = color;
}

/*
 *
 */
static void refresh_display(struct context *ctx)
{
    if (ctx->cursor_j != ctx->cursor_cur_j ||
        ctx->cursor_i != ctx->cursor_cur_i) {

        for (int i = 0; i < 8; ++i)
            for (int j = 0; j < 8; ++j)
                put_pixel(ctx, ctx->cursor_cur_i + i, ctx->cursor_cur_j + j, 0);
        for (int i = 0; i < 8; ++i)
            for (int j = 0; j < 8; ++j)
                put_pixel(ctx, ctx->cursor_i + i, ctx->cursor_j + j, 0xFFFFFFFF);

        ctx->cursor_cur_j = ctx->cursor_j;
        ctx->cursor_cur_i = ctx->cursor_i;
    }
}

/*
 *
 */
static void mouse_update_position(struct context *ctx,
                                  const struct input_event *ev)
{
    switch (ev->ev_type) {
    case INPUT_EV_CURSOR_REL:
        ctx->cursor_i -= ev->ev_data.cursor_pos.pos_y;
        ctx->cursor_j += ev->ev_data.cursor_pos.pos_x;
        break;
    case INPUT_EV_CURSOR_ABS:
        ctx->cursor_i = ev->ev_data.cursor_pos.pos_y;
        ctx->cursor_j = ev->ev_data.cursor_pos.pos_x;
        break;
    default:
        break;
    }

    if (ctx->cursor_j >= (int32_t)ctx->fb_params.width)
        ctx->cursor_j = ctx->fb_params.width - 1;
    if (ctx->cursor_j < 0)
        ctx->cursor_j = 0;

    if (ctx->cursor_i >= (int32_t)ctx->fb_params.height)
        ctx->cursor_i = ctx->fb_params.height - 1;
    if (ctx->cursor_i < 0)
        ctx->cursor_i = 0;
}

/*
 *
 */
static void input_device_handle_event(struct input_device *dev,
                                      const struct input_event *ev)
{
    // input_event_dump(ev);

    switch (ev->ev_type) {
    case INPUT_EV_KEY_PRESS:
    case INPUT_EV_KEY_RELEASE:
    case INPUT_EV_CURSOR_REL:
    case INPUT_EV_CURSOR_ABS:
        mouse_update_position(dev->ctx, ev);
        break;
    }
}

/*
 *
 */
static void input_device_destroy(struct input_device *dev)
{
    close(dev->fd);
    free((void *)dev->name);
    free(dev);
}

/*
 *
 */
static void main_loop_step(struct context *ctx)
{
    struct input_device *mouse = ctx->input_devices;
    struct input_event events[64];
    unsigned int count;
    ssize_t size;

    size = read(mouse->fd, events, sizeof(events));
    if (size <= 0)
        return;

    count = size / sizeof(struct input_event);
    for (unsigned int i = 0; i < count; ++i)
        input_device_handle_event(mouse, &events[i]);

    refresh_display(ctx);
}

/*
 *
 */
static int init_input_devices(struct context *ctx)
{
    struct dirent *entry;
    DIR *dir;

    dir = opendir("/dev");
    if (!dir) {
        perror("opendir(/dev)");
        return -1;
    }

    ctx->input_device_count = 0;
    ctx->input_devices = NULL;

    while ((entry = readdir(dir))) {
        struct input_device *dev;
        char path[PATH_MAX];
        int idx;
        int fd;

        if (sscanf(entry->d_name, "input%d", &idx) != 1)
            continue;

        /* debug: mouse only */
        if (strcmp(entry->d_name, "input1"))
            continue;

        sprintf(path, "/dev/%s", entry->d_name);
        fd = open(path, O_RDONLY);
        if (!fd) {
            printf("open(%s) failed: %s\n", path, strerror(errno));
            continue;
        }

        dev = calloc(1, sizeof(*dev));
        if (!dev) {
            printf("malloc(%s) failed: %s\n", path, strerror(errno));
            close(fd);
            break;
        }

        /* register input device */
        dev->fd = fd;
        dev->ctx = ctx;
        dev->name = strdup(entry->d_name);
        dev->next = ctx->input_devices;
        ctx->input_devices = dev;
        ctx->input_device_count++;
    }

    return 0;
}

/*
 *
 */
static int init_framebuffer(struct context *ctx)
{
    int fd = -1;

    fd = open("/dev/fb0", O_RDWR);
    ctx->fb_fd = fd;
    if (fd < 0) {
        perror("open()");
        return -1;
    }

    if (ioctl(fd, IO_FB_GET_PARAMS, &ctx->fb_params)) {
        perror("IO_FB_GET_PARAMS");
        return -1;
    }

    if (ioctl(fd, IO_FB_GET_BUFFER, &ctx->fb_back_buffer)) {
        perror("IO_FB_GET_BUFFER");
        return -1;
    }

    return 0;
}

/*
 *
 */
static void release_ctx(struct context *ctx)
{
    struct input_device *dev;

    dev = ctx->input_devices;
    while (dev) {
        struct input_device *next_dev;

        next_dev = dev->next;
        input_device_destroy(dev);
        dev = next_dev;
    }

    if (ctx->fb_fd != -1)
        close(ctx->fb_fd);

}

int main(void)
{
    struct context ctx;
    int ret;

    memset(&ctx, 0, sizeof(ctx));

    ret = init_framebuffer(&ctx);
    if (ret) {
        printf("init_framebuffer() failed");
        goto err;
    }

    ret = init_input_devices(&ctx);
    if (ret) {
        printf("init_input_devices() failed");
        goto err;
    }

    while (true)
        main_loop_step(&ctx);

err:
    release_ctx(&ctx);
    while(1);
}
