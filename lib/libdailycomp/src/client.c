#include <libdailycomp/client.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/un.h>
#include <unistd.h>

struct dailycomp_client {
    int sock_ev_fd;
    int server_fd;
};

/*
 * Allocate and initialize new client.
 */
struct dailycomp_client *dailycomp_client_new(void)
{
    struct dailycomp_client *client = NULL;
    struct sockaddr_un sun;
    int ret;

    client = calloc(1, sizeof(*client));
    if (!client)
        return NULL;

    client->server_fd = -1;
    client->sock_ev_fd = -1;

    memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    snprintf(sun.sun_path, SUN_PATH_SIZE, "dailycomp/client-%d", getpid());

    client->sock_ev_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (client->sock_ev_fd < 0)
        goto fail;

    ret = bind(client->sock_ev_fd, (void *)&sun, sizeof(sun));
    if (ret < 0)
        goto fail;

    /*
     * Connect to the server.
     */

    memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    snprintf(sun.sun_path, SUN_PATH_SIZE, DAILYCOMP_SOCK_PATH);

    client->server_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (client->server_fd < 0)
        goto fail;

    ret = connect(client->server_fd, (void *)&sun, sizeof(sun));
    if (ret < 0)
        goto fail;

    return client;

fail:
    dailycomp_client_destroy(client);
    return NULL;
}

/*
 * Free client.
 */
void dailycomp_client_destroy(struct dailycomp_client *client)
{
    if (client->server_fd != -1)
        close(client->server_fd);
    if (client->sock_ev_fd != -1)
        close(client->sock_ev_fd);

    free(client);
}

/*
 * Create and register a new dailycomp window.
 */
struct dailycomp_window *dailycomp_client_new_window(const char *title,
                                                     unsigned int width,
                                                     unsigned int height,
                                                     unsigned int pos_i,
                                                     unsigned int pos_j)
{
    return NULL;
}

/*
 *
 */
void dailycomp_window_destroy(struct dailycomp_window *win)
{
    free(win);
}
