#include <dirent.h>

int dirfd(DIR *dirp)
{
    return dirp->dd_fd;
}
