#include <kernel/file.h>
#include <kernel/kmalloc.h>
#include <kernel/vfs.h>
#include <uapi/kernel/net.h> /* struct msghdr */

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

    if (fops->open)
        ret = fops->open(file);

    if (ret != E_SUCCESS) {
        vfs_vnode_release(vnode);
        kfree(file);
        return PTR_ERR(ret);
    }

    return file;
}

void file_close(struct file *file)
{
    if (file->ops->close)
        file->ops->close(file);

    vfs_vnode_release(file->vnode);

    kfree(file);
}

error_t file_sendto(struct file *file, const char *data, size_t len, int flags,
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

error_t file_send(struct file *file, const char *data, size_t len, int flags)
{
    return file_sendto(file, data, len, flags, NULL, 0);
}

error_t file_recvfrom(struct file *file, const char *data, size_t len,
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

error_t file_recv(struct file *file, const char *data, size_t len, int flags)
{
    return file_recvfrom(file, data, len, flags, NULL, NULL);
}
