#include <dailyrun/socket.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "dailycomp.h"

static unsigned int last_window_id = 0;

void window_send_message(struct window *win, struct window_ev_message *ev)
{
    ssize_t sent;

    ev->window_id = win->id;

    sent = send(win->sock_fd, ev, sizeof(ev), 0);
    if (sent < 0)
        printf("send_message(%d): failed to send message: %m\n", win->id);
}

/*
 *
 */
void window_handle_event(struct window *win,
                         const struct window_ev_message *ev)
{
}

/*
 * Allocate and initialize a new window.
 */
struct window *window_new(const char *title, const char *sock_path,
                          unsigned int pos_i, unsigned int pos_j,
                          unsigned int width, unsigned int height)
{
    struct window *win;
    struct sockaddr_un sun;
    size_t dirty_size;
    int shm_fd;
    int ret;

    win = calloc(1, sizeof(*win));
    if (!win)
        return NULL;

    win->id = ++last_window_id;
    win->pos_i = pos_i;
    win->pos_j = pos_j;
    win->buffer_width = width;
    win->buffer_height = height;
    win->buffer_stride = width * sizeof(rgba_t);
    INIT_LLIST_NODE(win->this);
    strlcpy(win->title, title, sizeof(win->title));
    snprintf(win->buffer_shm_id, sizeof(win->buffer_shm_id),
             "dailycomp/windows/id-%d", win->id);

    dirty_size = width * height;
    if (dirty_size % 8)
        dirty_size += dirty_size % 8;
    dirty_size /= 8; /* 1 bit per pixel */

    win->dirty_size = dirty_size;
    win->dirty = calloc(dirty_size, sizeof(uint8_t));
    if (!win->dirty) {
        printf("%s: failed to allocate dirty tracker", title);
        goto fail;
    }

    /*
     * Connect to the client's UNIX socket.
     */

    memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    strlcpy(sun.sun_path, sock_path, sizeof(sun.sun_path));

    win->sock_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (win->sock_fd < 0) {
        printf("%s: socket(AF_UNIX): %m\n", title);
        goto fail;
    }

    ret = connect(win->sock_fd, (void *)&sun, sizeof(sun));
    if (ret < 0) {
        printf("%s: connect(%s): %m\n", title, sock_path);
        goto fail;
    }

    /*
     * Map shared pixel buffer used by the client.
     */

    shm_fd = shm_open(win->buffer_shm_id,
                      O_RDONLY | O_CREAT | O_EXCL,
                      S_IRWO | S_IRWG | S_IRWU);
    if (shm_fd < 0) {
        printf("shm_open(%s): %m\n", win->buffer_shm_id);
        goto fail;
    }

    win->buffer = mmap(NULL, win->buffer_height * win->buffer_stride,
                       PROT_READ, MAP_PRIVATE, shm_fd, 0);
    close(shm_fd);
    if (win->buffer == MAP_FAILED) {
        win->buffer = NULL;
        goto fail;
    }

    return win;

fail:
    window_destroy(win);
    return NULL;
}

/*
 * Free a window.
 */
void window_destroy(struct window *win)
{
    llist_remove(&win->this);

    shm_unlink(win->buffer_shm_id);
    if (win->buffer)
        munmap(win->buffer, win->buffer_height * win->buffer_stride);

    if (win->dirty)
        free(win->dirty);

    if (win->sock_fd != -1)
        close(win->sock_fd);

    free((void *)win);
}
