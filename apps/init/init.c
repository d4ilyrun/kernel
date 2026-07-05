#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include <limits.h>
#include <dirent.h>
#include <sys/dirent.h>

#include <libinput/libinput.h>

static int printer(const char *device)
{
    struct input_event events[1024];
    char path[PATH_MAX];
    ssize_t size;
    int fd;

    snprintf(path, sizeof(path), "/dev/%s", device);
    printf("new input device found: %s\n", device);

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("open");
        goto end;
    }

    while ((size = read(fd, events, sizeof(events))) > 0) {
        for (size_t i = 0; i < size / sizeof(struct input_event); ++i) {
            input_event_dump(&events[i]);
        }
    }

end:
    return 0;
}

int main(void)
{
    DIR *dir;
    struct dirent *entry;

    dir = opendir("/dev");
    if (!dir) {
        perror("opendir");
        goto end;
    }

    while ((entry = readdir(dir))) {
        int idx;

        if (sscanf(entry->d_name, "input%d", &idx) != 1)
            continue;

        if (fork() == 0)
            printer(entry->d_name);
    }

end:
    while (1);
    return EXIT_SUCCESS;
}
