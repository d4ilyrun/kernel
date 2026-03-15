#include <dirent.h>
#include <sys/dirent.h>

/* syscall */
extern ssize_t _getdents(int fd, void *buf, size_t size, int flags);

/*
 *
 */
static int refill_buf(DIR *dirp)
{
    ssize_t size;

    size = _getdents(dirp->dd_fd, dirp->dd_buf, dirp->dd_buf_size, 0);
    if (size < 0)
        return (int)size;

    dirp->dd_size = size;
    dirp->dd_loc = 0;

    /* EOF was reached */
    if (size == 0)
        return 0;

    return size;
}

/*
 *
 */
struct dirent *readdir(DIR *dirp)
{
    struct posix_dent *dent;
    struct dirent *dirent;

    if (dirp->dd_loc >= dirp->dd_size) {
        if (refill_buf(dirp) <= 0)
            return NULL;
    }

    dent = (void *)dirp->dd_buf + dirp->dd_loc;
    dirent = &dent->dirent;

    if (dirent->d_reclen <= 0 ||
        dirent->d_reclen > (unsigned long)dirp->dd_buf_size - dirp->dd_loc)
        return NULL;

    dirp->dd_loc += dirent->d_reclen;

    return dirent;
}
