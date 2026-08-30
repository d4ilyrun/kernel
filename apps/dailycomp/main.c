#include <dailyrun/framebuffer.h>
#include <dailyrun/input.h>

#include <libinput/libinput.h>

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <unistd.h>

#include "dailycomp.h"

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
static void draw_cursor(struct context *ctx)
{

    if (ctx->cursor_j == ctx->cursor_cur_j &&
        ctx->cursor_i == ctx->cursor_cur_i)
        return; /* position did not change */

    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j)
            put_pixel(ctx, ctx->cursor_cur_i + i, ctx->cursor_cur_j + j, 0);
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j)
            put_pixel(ctx, ctx->cursor_i + i, ctx->cursor_j + j, 0xFFFFFFFF);

    ctx->cursor_cur_j = ctx->cursor_j;
    ctx->cursor_cur_i = ctx->cursor_i;
}

/*
 *
 */
static void handle_client_message(struct context *ctx,
                                  struct window_ev_message *msg)
{
    struct window *win;

    if (msg->magic != WINDOW_EV_MAGIC)
        return;

    switch (msg->type) {
    case WINDOW_EV_NEW_REQUEST:
        win = window_new(msg->new.title,
                         msg->new.sock_path,
                         msg->new.pos_i, msg->new.pos_j,
                         msg->new.width, msg->new.height);
        if (win == NULL) {
            printf("failed to create window: %s", msg->new.title);
            break;
        }

        msg->type = WINDOW_EV_NEW_RESPONSE;
        strncpy(msg->new.shm_id, win->buffer_shm_id, sizeof(msg->new.shm_id));
        window_send_message(win, msg);
        return;

    case WINDOW_EV_REGION_DIRTY:
        /* event handled by the window itself */
        FOREACH_LLIST_ENTRY(win, &ctx->windows, this) {
            if (win->id == msg->window_id)
                return window_handle_event(win, msg);
        }

        printf("ev: unknown window id: %#08x\n", msg->window_id);
        return;

    case WINDOW_EV_NEW_RESPONSE:
        /* event sent by the server and handled by the client. */
        return;

    default:
        return;
    }
}

/*
 *
 */
static void refresh_screen(struct context *ctx)
{
    draw_cursor(ctx);
}

/*
 *
 */
static void main_loop_step(struct context *ctx)
{
    struct input_device *dev;
    struct window_ev_message win_events[64];
    unsigned int win_event_count = 0;
    struct window *win;
    ssize_t size;

    /* TODO: Add and use poll() syscall */
    size = read(ctx->ev_sock_fd, win_events, sizeof(win_events));
    if (size >= 0)
        win_event_count = size / sizeof(struct window_ev_message);

    for (int i = 0; i < win_event_count; ++i)
        handle_client_message(ctx, &win_events[i]);

    FOREACH_LLIST_ENTRY(dev, &ctx->input_devices, this)
        input_device_handle_events(dev);

    refresh_screen(ctx);
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

    while ((entry = readdir(dir))) {
        struct input_device *dev;
        char path[PATH_MAX];
        int idx;

        if (sscanf(entry->d_name, "input%d", &idx) != 1)
            continue;

        sprintf(path, "/dev/%s", entry->d_name);
        dev = input_device_new(ctx, entry->d_name, path);
        if (!dev) {
            printf("failed to init input device: %s\n", path);
            continue;
        }

        llist_add(&ctx->input_devices, &dev->this);
    }

    return 0;
}

/*
 * Open the UNIX socket used by the clients to send messages to the server.
 */
static int init_event_socket(struct context *ctx, const char *sock_path)
{
    struct sockaddr_un sun;
    int ret;

    memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    strlcpy(sun.sun_path, sock_path, sizeof(sun.sun_path));

    ctx->ev_sock_fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (ctx->ev_sock_fd < 0) {
        printf("socket(AF_UNIX): %m\n");
        return -1;
    }

    ret = bind(ctx->ev_sock_fd, (void *)&sun, sizeof(sun));
    if (ret < 0) {
        printf("connect(%s): %m\n", sock_path);
        return -1;
    }

    printf("server listening for events on '%s'\n", sock_path);

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
    struct input_device *next;

    FOREACH_LLIST_ENTRY_SAFE(dev, next, &ctx->input_devices, this)
        input_device_destroy(dev);

    if (ctx->fb_fd != -1)
        close(ctx->fb_fd);
    if (ctx->ev_sock_fd != -1)
        close(ctx->ev_sock_fd);
}

int main(int argc, char **argv)
{
    struct context ctx;
    int ret;

    memset(&ctx, 0, sizeof(ctx));
    INIT_LLIST(ctx.input_devices);
    INIT_LLIST(ctx.windows);
    ctx.ev_sock_fd = -1;
    ctx.fb_fd = -1;

    printf("%s !!!\n", argv[0]);

    ret = init_event_socket(&ctx, DAILYCOMP_SOCK_PATH);
    if (ret) {
        printf("init_event_socket() failed\n");
        goto err;
    }

    ret = init_framebuffer(&ctx);
    if (ret) {
        printf("init_framebuffer() failed\n");
        goto err;
    }

    ret = init_input_devices(&ctx);
    if (ret) {
        printf("init_input_devices() failed\n");
        goto err;
    }

    while (true)
        main_loop_step(&ctx);

err:
    release_ctx(&ctx);
    while(1);
}
