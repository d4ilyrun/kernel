#ifndef UAPI_FCNTL_H
#define UAPI_FCNTL_H

#include <fcntl.h>

#define O_READABLE(_flags) ((_flags & O_ACCMODE) != O_WRONLY)
#define O_WRITABLE(_flags) ((_flags & O_ACCMODE) != O_RDONLY)

#define S_IRWU (S_IRUSR | S_IWUSR)
#define S_IRWG (S_IRGRP | S_IWGRP)
#define S_IRWO (S_IROTH | S_IWOTH)

#endif /* UAPI_FCNTL_H */
