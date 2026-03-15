#ifndef _SYS_DIRENT_H
#define _SYS_DIRENT_H

#include <sys/types.h>

typedef struct _dirdesc {
    int dd_fd;
    long dd_buf_size; /* sizeof(dd_buf) */
    char *dd_buf;
    long dd_loc;
    int dd_size; /* number of bytes occupied by the structs inside buf. */
    long dd_seek;
} DIR;

#define posix_dent_fields \
    reclen_t d_reclen;    \
    off_t d_off;          \
    ino_t d_ino;          \
    unsigned char d_type; \
    char d_name[];

struct dirent {
    posix_dent_fields;
};

struct posix_dent {
    union {
        struct dirent dirent;
        struct {
            posix_dent_fields;
        };
    };
};

#undef posix_dent_fields

#define DT_BLK S_IFBLK   // Block special.
#define DT_CHR S_IFCHR   // Character special.
#define DT_DIR S_IFDIR   // Directory.
#define DT_FIFO S_IFIFO  // FIFO special.
#define DT_LNK S_IFLNK   // Symbolic link.
#define DT_REG S_IFREG   // Regular.
#define DT_SOCK S_IFSOCK // Socket.
#define DT_UNKNOWN (-1)

ssize_t getdents(int fd, void *buf, size_t size, int flags);

#endif
