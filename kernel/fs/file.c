#include <kernel/file.h>
#include <kernel/kmalloc.h>
#include <kernel/process.h>
#include <kernel/vfs.h>
#include <uapi/kernel/net.h> /* struct msghdr */
#include <uapi/unistd.h>

struct file *file_open(struct vnode *vnode, const struct file_operations *fops)
{
    struct file *file;
    error_t ret = E_SUCCESS;

    if (fops == NULL)
        return PTR_ERR(E_NOT_SUPPORTED);

    file = kcalloc(1, sizeof(*file), KMALLOC_KERNEL);
    if (file == NULL)
        return PTR_ERR(E_NOMEM);

    file->ops = fops;
    file->vnode = vfs_vnode_acquire(vnode, NULL);

    INIT_SPINLOCK(file->lock);

    atomic_write(&file->refcount, 0);
    file_get(file);

    if (fops->open)
        ret = fops->open(file);

    if (ret != E_SUCCESS) {
        file_put(file);
        return PTR_ERR(ret);
    }

    return file;
}

void __file_put(struct file *file)
{
    if (file->ops->close)
        file->ops->close(file);

    vfs_vnode_release(file->vnode);

    kfree(file);
}

off_t default_file_seek(struct file *file, off_t off, int whence)
{
    enum vnode_type type = file->vnode->type;

    if (type == VNODE_SOCKET || type == VNODE_FIFO)
        return -E_SEEK_PIPE;

    spinlock_acquire(&file->lock);

    switch (whence) {
    case SEEK_CUR:
        file->pos += off;
        break;
    case SEEK_END:
        file->pos = file_size(file) + off;
        break;
    case SEEK_SET:
        file->pos = off;
        break;
    default:
        spinlock_release(&file->lock);
        return -E_INVAL;
    }

    spinlock_release(&file->lock);
    return file->pos;
}

ssize_t file_sendto(struct file *file, const char *data, size_t len, int flags,
                    struct sockaddr *addr, socklen_t addrlen)
{
    struct iovec iov = {
        .iov_base = (char *)data,
        .iov_len = len,
    };
    struct msghdr msg = {
        .msg_name = addr,
        .msg_namelen = addrlen,
        .msg_iov = &iov,
        .msg_iovlen = 1,
    };

    return file_sendmsg(file, &msg, flags);
}

ssize_t file_send(struct file *file, const char *data, size_t len, int flags)
{
    return file_sendto(file, data, len, flags, NULL, 0);
}

ssize_t file_recvfrom(struct file *file, const char *data, size_t len,
                      int flags, struct sockaddr *addr, size_t *addrlen)
{
    error_t ret;
    struct iovec iov = {
        .iov_base = (char *)data,
        .iov_len = len,
    };
    struct msghdr msg = {
        .msg_name = addr,
        .msg_namelen = addrlen ? *addrlen : 0,
        .msg_iov = &iov,
        .msg_iovlen = 1,
    };

    ret = file_recvmsg(file, &msg, flags);

    if (addrlen)
        *addrlen = msg.msg_namelen;

    return ret;
}

ssize_t file_recv(struct file *file, const char *data, size_t len, int flags)
{
    return file_recvfrom(file, data, len, flags, NULL, NULL);
}

/*
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/lseek.html
 */
off_t sys_lseek(int fd, off_t off, int whence)
{
    struct file *file;

    file = process_file_get(current->process, fd);
    if (!file)
        return -E_BAD_FD;

    off = file_seek(file, off, whence);
    process_file_put(current->process, file);

    return off;
}
