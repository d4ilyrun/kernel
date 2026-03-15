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

struct dirent {
    long d_ino;
    unsigned char d_type;
    off_t d_off;
    unsigned short d_reclen;
    char d_name[];
};

struct posix_dent {
    reclen_t d_reclen;    // Length of this entry, including trailing
    union {
        struct dirent dirent;
        struct {
            unsigned char d_type; // File type or unknown-file-type indication.
            ino_t d_ino;   // File serial number.
            char d_name[]; // Filename string of this entry.
        };
    };
};

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
