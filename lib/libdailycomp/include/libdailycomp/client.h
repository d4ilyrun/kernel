#ifndef _DAILYCOMP_CLIENT_H
#define _DAILYCOMP_CLIENT_H

#include <libdailycomp/dailycomp.h>

#include <stdint.h>

struct dailycomp_client;

struct dailycomp_client *dailycomp_client_new(void);
void dailycomp_client_destroy(struct dailycomp_client *);

/*
 * Client side representation of a window.
 */
struct dailycomp_window {
    uint32_t                 id;
    struct dailycomp_client *client;
};

struct dailycomp_window *dailycomp_client_new_window(const char *title,
                                                     unsigned int width,
                                                     unsigned int height,
                                                     unsigned int pos_i,
                                                     unsigned int pos_j);
void dailycomp_window_destroy(struct dailycomp_window *);

#endif /* _DAILYCOMP_CLIENT_H */
