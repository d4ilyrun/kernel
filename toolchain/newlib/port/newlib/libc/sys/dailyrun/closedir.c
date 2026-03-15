#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>

int closedir(DIR *dirp)
{
    int ret;

    ret = close(dirp->dd_fd);
    if (ret)
        return ret;

    free(dirp->dd_buf);
    free(dirp);

    return 0;
}
