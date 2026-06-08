#ifndef _SYS_UN_H
#define _SYS_UN_H

#include <sys/socket.h>

#define SUN_PATH_SIZE 108

struct sockaddr_un {
    sa_family_t  sun_family;              // AF_UNIX.
    char         sun_path[SUN_PATH_SIZE]; // Socket pathname.
};

#endif // !_SYS_UN_H
