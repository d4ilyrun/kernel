#include <kernel/device.h>
#include <kernel/devices/timer.h>
#include <kernel/error.h>
#include <kernel/file.h>
#include <kernel/kmalloc.h>
#include <kernel/vfs.h>

#include <libalgo/linked_list.h>
#include <utils/container_of.h>
#include <utils/macro.h>

#include <string.h>

/** Global list of currently registered devices */
DECLARE_LLIST(registered_devices);

struct devtmpfs {
    vnode_t root;
};

static vfs_ops_t devtmpfs_vfs_ops;
static vnode_ops_t devtmpfs_vnode_ops;

error_t device_register(device_t *dev)
{
    dev->vnode = NULL;
    llist_add(&registered_devices, &dev->this);
    return E_SUCCESS;
}

static inline struct vnode *device_acquire_vnode(struct device *dev)
{
    bool new;
    struct stat *stat;

    dev->vnode = vfs_vnode_acquire(dev->vnode, &new);
    if (!new)
        return dev->vnode;

    dev->vnode->operations = &devtmpfs_vnode_ops;
    dev->vnode->pdata = dev;

    /*
     * TODO: Set vnode rdev.
     * TODO: Set vnode uid/gid.
     * TODO: Determine device's permission.
     */
    stat = &dev->vnode->stat;
    clock_get_time(&stat->st_mtim);
    stat->st_ctim = stat->st_mtim;
    stat->st_mode = S_IRWU | S_IRWG | S_IRWO;
    stat->st_nlink = 1;

    return dev->vnode;
}

struct file *device_open(device_t *dev)
{
    struct vnode *vnode = device_acquire_vnode(dev);
    struct file *file = file_open(vnode, dev->fops);
    vfs_vnode_release(vnode);
    return file;
}

struct device *device_find(const char *name)
{
    struct device *dev;

    FOREACH_LLIST (node, &registered_devices) {
        dev = container_of(node, device_t, this);
        if (!strcmp(dev->name, name))
            return dev;
    }

    return NULL;
}

static vnode_t *
devtmpfs_vnode_create(vnode_t *node, const char *name, vnode_type type)
{
    /* This fs only lists existing devices, cannot remove them */
    UNUSED(node);
    UNUSED(name);
    UNUSED(type);
    return PTR_ERR(E_NOT_SUPPORTED);
}

static error_t devtmpfs_vnode_remove(vnode_t *node, const char *child)
{
    /* This fs only lists existing devices, cannot remove them */
    UNUSED(node);
    UNUSED(child);
    return E_NOT_SUPPORTED;
}

static vnode_t *
devtmpfs_vnode_lookup(vnode_t *node, const path_segment_t *child)
{
    vnode_t *root = node->fs->operations->root(node->fs);
    device_t *dev;

    if (node != root)
        return PTR_ERR(E_INVAL);

    FOREACH_LLIST (node, &registered_devices) {
        dev = container_of(node, device_t, this);
        if (path_segment_is(device_name(dev), child))
            break;
        dev = NULL;
    }

    if (dev == NULL)
        return PTR_ERR(E_NODEV);

    return device_acquire_vnode(dev);
}

static struct file *devtmpfs_vnode_open(struct vnode *vnode)
{
    struct device *dev = vnode->pdata;
    return file_open(vnode, dev->fops);
}

static void devtmpfs_vnode_release(struct vnode *vnode)
{
    struct device *dev = vnode->pdata;
    dev->vnode = NULL;
}

static vnode_ops_t devtmpfs_vnode_ops = {
    .create = devtmpfs_vnode_create,
    .remove = devtmpfs_vnode_remove,
    .lookup = devtmpfs_vnode_lookup,
    .open = devtmpfs_vnode_open,
    .release = devtmpfs_vnode_release,
};

static vnode_t *devtmpfs_root(vfs_t *vfs)
{
    struct devtmpfs *devtmpfs = vfs->pdata;
    return vfs_vnode_acquire(&devtmpfs->root, NULL);
}

static void devtmpfs_delete(vfs_t *vfs)
{
    UNUSED(vfs);
}

static vfs_ops_t devtmpfs_vfs_ops = {
    .root = devtmpfs_root,
    .delete = devtmpfs_delete,
};

static vfs_t *devtmpfs_new(u32 start, u32 end)
{
    struct devtmpfs *devtmpfs;
    struct vfs *vfs;

    UNUSED(start);
    UNUSED(end);

    vfs = kcalloc(1, sizeof(vfs_t), KMALLOC_KERNEL);
    if (vfs == NULL)
        return PTR_ERR(E_NOMEM);

    devtmpfs = kcalloc(1, sizeof(*devtmpfs), KMALLOC_KERNEL);
    if (devtmpfs == NULL) {
        kfree(vfs);
        return PTR_ERR(E_NOMEM);
    }

    vfs->operations = &devtmpfs_vfs_ops;
    vfs->pdata = devtmpfs;

    devtmpfs->root.fs = vfs;
    devtmpfs->root.type = VNODE_DIRECTORY;
    devtmpfs->root.operations = &devtmpfs_vnode_ops;
    devtmpfs->root.refcount = 1; // Do not release it

    return vfs;
}

DECLARE_FILESYSTEM(devtmpfs, devtmpfs_new);
