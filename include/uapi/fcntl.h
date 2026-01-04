#ifndef UAPI_FCNTL_H
#define UAPI_FCNTL_H

#include <fcntl.h>

#ifndef O_EXEC
#define O_EXEC 0
#endif

#ifndef O_SEARCH
#define O_SEARCH O_EXEC
#endif

#define O_READABLE(_flags) ((_flags & O_ACCMODE) != O_WRONLY)
#define O_WRITABLE(_flags) ((_flags & O_ACCMODE) != O_RDONLY)

#define S_IRWU (S_IRUSR | S_IWUSR)
#define S_IRWG (S_IRGRP | S_IWGRP)
#define S_IRWO (S_IROTH | S_IWOTH)

#endif /* UAPI_FCNTL_H */
