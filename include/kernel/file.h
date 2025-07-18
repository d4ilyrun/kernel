/**
 * @defgroup kernel_fs_file File
 * @ingroup kernel_fs
 * @brief File structure
 *
 * @{
 */
#ifndef KERNEL_FILE_H
#define KERNEL_FILE_H

#include <kernel/atomic.h>
#include <kernel/error.h>
#include <kernel/spinlock.h>

struct vnode;
struct sockaddr;
struct msghdr;

/** Opened file description.
 *
 * @note This struct is also used by pseudo-filesystems, such as the
 * sockfs, to allocated sockets or other objects. According to the UNIX
 * philosophy, everything is a file.
 */
struct file {
    off_t pos;                         ///< Current offset into the file
    void *priv;                        ///< Private data used by the driver
    struct vnode *vnode;               ///< The file's vnode in the VFS
    const struct file_operations *ops; ///< @see file_operations
    atomic_t refcount;                 ///< Number of references to this file
    spinlock_t lock;                   ///< Synchronization lock
    int flags;                         ///< Parameter flags (@see man 2 open)
};

void __file_put(struct file *file);

/** Increment an open file description's reference count.
 *  @return The open file description.
 */
static inline struct file *file_get(struct file *file)
{
    atomic_inc(&file->refcount);
    return file;
}

/** Decrement an open file description's reference count.
 *
 * If this was the last reference to this open file description,
 * the underlying structure is released.
 */
static inline void file_put(struct file *file)
{
    int count;

    count = atomic_dec(&file->refcount);
    if (count > 1)
        return;

    __file_put(file);
}

/** Operations that can be performed on an opened file.
 *  @struct file_operations
 */
struct file_operations {
    /**
     * Filesystem-specific open function (optional).
     *
     * If present it is called when opening a new file, and can be used
     * to initialize the file's @c priv field.
     */
    error_t (*open)(struct file *);
    /**
     * Called when closing a file (optional).
     *
     * If present, it should release any memory allocated by the driver to
     * keep trace of the file's internal state.
     */
    void (*close)(struct file *);
    /** Write a buffer to the file at the current position. */
    ssize_t (*write)(struct file *, const char *, size_t);
    /** Read the content at the current position in the file. */
    ssize_t (*read)(struct file *, char *, size_t);
    /** Compute the file's total size in memory */
    size_t (*size)(struct file *);
    /** Associate a socket with a local address */
    error_t (*bind)(struct file *, struct sockaddr *addr, size_t len);
    /** Connect to a remote host */
    error_t (*connect)(struct file *, struct sockaddr *addr, size_t len);
    /** Send a message through an endpoint (socket) */
    ssize_t (*sendmsg)(struct file *, const struct msghdr *, int flags);
    /** Send a message through an endpoint (socket) */
    ssize_t (*recvmsg)(struct file *, struct msghdr *, int flags);
    /** Reposition the open file description offset.
     *
     * @note It is possible for a description's offset to go beyond the
     *       backing file's size. In this case subsequent reads will return
     *       empty bytes until data is actually written into the gap.
     *
     * @return The updated offset
     *
     * @see man 2 lseek
     */
    off_t (*seek)(struct file *, off_t, int whence);
};

/** Create a new file structure */
struct file *file_open(struct vnode *, const struct file_operations *);

/** Reposition the open file description offset
 *
 * This is the default implementation for @ref file_operations.seek().
 * It simply increments the description's offset.
 */
off_t default_file_seek(struct file *file, off_t off, int whence);

/** Free a file struct and release its content */
static inline void file_close(struct file *file)
{
    file_put(file);
}

/** Update the file's vnode's last access time. */
void file_accessed(struct file *file);
/** Update the file's vnode's last modification time. */
void file_modified(struct file *file);

#define __file_ops(_default, _file, _ops, ...)                             \
    (_file->ops->_ops ? _file->ops->_ops(_file __VA_OPT__(, ) __VA_ARGS__) \
                      : _default)

#define file_ops(_file, _ops, ...) \
    __file_ops(E_NOT_SUPPORTED, _file, _ops, __VA_ARGS__)

#define file_size(file) file_ops(file, size)

#define file_write(file, buf, len) file_ops(file, write, buf, len)
#define file_read(file, buf, len) file_ops(file, read, buf, len)
#define file_seek(file, off, whence) file_ops(file, seek, off, whence)

#define file_bind(file, addr, len) file_ops(file, bind, addr, len)
#define file_connect(file, addr, len) file_ops(file, connect, addr, len)

#define file_sendmsg(file, msg, flags) file_ops(file, sendmsg, msg, flags)
ssize_t file_send(struct file *file, const char *data, size_t len, int flags);
ssize_t file_sendto(struct file *file, const char *data, size_t len, int flags,
                    struct sockaddr *addr, size_t addrlen);

#define file_recvmsg(file, msg, flags) file_ops(file, recvmsg, msg, flags)
ssize_t file_recv(struct file *file, const char *data, size_t len, int flags);
ssize_t file_recvfrom(struct file *file, const char *data, size_t len,
                      int flags, struct sockaddr *addr, size_t *addrlen);

#endif /* KERNEL_FILE_H */

/** @} */
