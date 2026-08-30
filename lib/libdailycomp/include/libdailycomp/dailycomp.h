#ifndef _LIBDAILYCOMP_H
#define _LIBDAILYCOMP_H

#include <stdint.h>

#define DAILYCOMP_SOCK_PATH "dailycomp/server"

typedef union rgba {
    uint32_t rgba;
    struct {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t a;
    };
} rgba_t;

#define WINDOW_EV_MAGIC 0x0987EAF8

#define WINDOW_EV_MAX_SOCKPATH_SIZE 64
#define WINDOW_EV_MAX_SHM_SIZE      64
#define WINDOW_EV_MAX_TITLE_SIZE    64

enum window_ev_type {
    WINDOW_EV_NEW_REQUEST,  /* Client asks to create a new window. */
    WINDOW_EV_NEW_RESPONSE, /* Response from the server to a window creation request. */
    WINDOW_EV_REGION_DIRTY, /* Mark a window region as dirty (must be redrawn) */
    WINDOW_EV_REDRAW,       /* Redraw the entire window */
    WINDOW_EV_COUNT,
    WINDOW_EV_ERROR,
    WINDOW_EV_MAX = WINDOW_EV_COUNT - 1,
};

/* Used during window creation requests.
 *
 * Events: WINDOW_EV_NEW_REQUEST, WINDOW_EV_NEW_RESPONSE
 */
struct window_ev_new {
    char        title[WINDOW_EV_MAX_TITLE_SIZE];
    char        sock_path[WINDOW_EV_MAX_SOCKPATH_SIZE];
    char        shm_id[WINDOW_EV_MAX_SHM_SIZE];
    int32_t     pos_i;     /* Window's starting position. */
    int32_t     pos_j;
    int32_t     width;     /* Window's starting dimensions. */
    int32_t     height;
    int32_t     stride;
};

/*
 *
 */
struct window_ev_region {
    int32_t             pos_i;
    int32_t             pos_j;
    int32_t             width;
    int32_t             height;
};

enum window_error {
    WINDOW_ERR_NOMEM,
};

/* Sent to report an error.
 *
 * Events: WINDOW_EV_ERROR
 */
struct window_ev_error {
    enum window_error   error;
};

struct window_ev_message {
    uint32_t            magic;
    uint32_t            window_id;
    enum window_ev_type type;
    union {
        struct window_ev_new new;
        struct window_ev_region region;
        struct window_ev_error error;
    };
};

const char *window_ev_to_str(enum window_ev_type);

#endif /* _LIBDAILYCOMP_H */
