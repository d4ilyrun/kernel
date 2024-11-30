#include <kernel/file.h>
#include <kernel/kmalloc.h>
#include <kernel/vfs.h>

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
