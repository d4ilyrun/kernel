#include <dailyrun/input.h>

#include <libinput/libinput.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "dailycomp.h"

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
 * Handle one input event.
 */
static void input_device_handle_event(struct input_device *dev,
                                      const struct input_event *ev)
{
    // input_event_dump(ev);

    switch (ev->ev_type) {
    case INPUT_EV_KEY_PRESS:
    case INPUT_EV_KEY_RELEASE:
        break;

    case INPUT_EV_CURSOR_REL:
    case INPUT_EV_CURSOR_ABS:
        mouse_update_position(dev->ctx, ev);
        break;
    }
}

/*
 * Fetch and process all currently pending input events for a device.
 */
void input_device_handle_events(struct input_device *dev)
{
    struct input_event events[64];
    unsigned int count;
    ssize_t size;

    size = read(dev->fd, events, sizeof(events));
    if (size <= 0)
        return;

    count = size / sizeof(struct input_event);
    for (unsigned int i = 0; i < count; ++i)
        input_device_handle_event(dev, &events[i]);
}

/*
 * Allocate and initialize an input device.
 */
struct input_device *input_device_new(struct context *ctx, const char *name,
                                      const char *path)
{
    struct input_device *dev = NULL;
    int fd = -1;

    fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        printf("open(%s) failed: %s\n", path, strerror(errno));
        goto fail;
    }

    dev = calloc(1, sizeof(*dev));
    if (!dev) {
        printf("malloc(%s) failed: %s\n", path, strerror(errno));
        goto fail;
    }

    INIT_LLIST_NODE(dev->this);
    dev->fd = fd;
    dev->ctx = ctx;
    dev->name = strdup(name);

    return dev;

fail:
    if (dev)
        input_device_destroy(dev);
    if (fd != -1)
        close(fd);
    return NULL;
}

/*
 * Free a device.
 */
void input_device_destroy(struct input_device *dev)
{
    llist_remove(&dev->this);

    if (dev->name)
        free((void *)dev->name);

    close(dev->fd);
    free(dev);
}
