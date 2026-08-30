#ifndef _DAILYCOMP_H
#define _DAILYCOMP_H

#include <dailyrun/framebuffer.h>

#include <libalgo/linked_list.h>
#include <libdailycomp/dailycomp.h>

#include <stdint.h>

struct input_device {
    struct context *ctx;
    const char     *name;
    int             fd;
    node_t          this;
};

struct input_device *
input_device_new(struct context *ctx, const char *name, const char *path);
void input_device_destroy(struct input_device *dev);
void input_device_handle_events(struct input_device *dev);

enum window_status_flag {
    WINDOW_STATUS_FULL_REDRAW = 0x1, /* Redraw the entire window (e.g. when moved) */
};

struct window {
    uint32_t        id; /* unique identifier, copied inside event messages. */
    char            title[WINDOW_EV_MAX_TITLE_SIZE];
    int             sock_fd; /* used to send events to the client. */
    unsigned int    status; /* status flags. */

    /* Window coordinates. */
    int             pos_i;
    int             pos_j;

    /*
     * Window's pixel buffer (RGBA bitmap).
     *
     * Shared memory between server & client.
     */
    void           *buffer;
    char            buffer_shm_id[WINDOW_EV_MAX_SHM_SIZE];
    unsigned int    buffer_width;
    unsigned int    buffer_height;
    unsigned int    buffer_stride;

    /* Private buffer of dirty pixels (1 bit per pixel). */
    void           *dirty;
    size_t          dirty_size;


    node_t          this;
};

struct window *window_new(const char *title, const char *sock_path,
                          unsigned int pos_i, unsigned int pos_j,
                          unsigned int width, unsigned int height);
void window_destroy(struct window *);
void window_handle_event(struct window *, const struct window_ev_message *);
void window_send_message(struct window *, struct window_ev_message *);

static inline void
window_send_error(struct window *win, enum window_error error)
{
    struct window_ev_message msg = {0};

    msg.type = WINDOW_EV_ERROR;
    msg.error.error = error;

    return window_send_message(win, &msg);
}

struct context {
    int                 ev_sock_fd; /* socket used to communicate with clients. */

    llist_t             input_devices;
    llist_t             windows; /* alive windows, sorted by z-axis. */

    /* framebuffer context */
    int                 fb_fd;
    struct fb_params    fb_params;
    struct fb_buffer    fb_back_buffer;

    /* cursor context */
    int                 cursor_i;
    int                 cursor_j;
    int                 cursor_cur_i;
    int                 cursor_cur_j;
};

#endif /* _DAILYCOMP_H */
