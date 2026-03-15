#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/lock.h>
#include <unistd.h>

#define DIR_BUFSIZE 512

static DIR *__fdopendir(int fd)
{
    DIR *dirp;

    dirp = calloc(1, sizeof(DIR));
    if (dirp == NULL) {
        close(fd);
        return NULL;
    }

    dirp->dd_fd = fd;
    dirp->dd_buf_size = DIR_BUFSIZE;
    dirp->dd_buf = malloc(dirp->dd_buf_size);
    if (dirp->dd_buf == NULL) {
        free(dirp);
        close(fd);
        return NULL;
    }

    return dirp;
}

DIR *fdopendir(int fd)
{
    /* TODO: Set CLOEXEC on fd via fcntl(). */
    return __fdopendir(fd);
}

DIR *opendir(const char *name)
{
    int fd;


    fd = open(name, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        return NULL;

    return __fdopendir(fd);
}
