#define LOG_PREFIX "dev"

#include <kernel/device.h>
#include <kernel/error.h>
#include <kernel/file.h>
#include <kernel/init.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/vfs.h>
#include <kernel/timer.h>

#include <libalgo/linked_list.h>
#include <utils/compiler.h>
#include <utils/container_of.h>
#include <utils/macro.h>
#include <utils/math.h>

#include <stdalign.h>
#include <string.h>
#include <dirent.h>

/** Global list of currently registered devices */
static DECLARE_LLIST(registered_devices);
static DECLARE_SPINLOCK(registered_devices_lock); // TODO: Use RW-lock

struct devtmpfs {
    vnode_t root;
};

static vfs_ops_t devtmpfs_vfs_ops;
static vnode_ops_t devtmpfs_vnode_ops;

/*
 * Make sure that a device's name is valid before registering it.
 */
static error_t device_check_name(const struct device *new)
{
    struct device *existing;

    ASSERT(spinlock_is_held(&registered_devices_lock));

    if (!new->name)
        return E_INVAL;

    FOREACH_LLIST_ENTRY(existing, &registered_devices, this) {
        if (!strcmp(existing->name, new->name)) {
            return E_EXIST;
        }
    }

    return E_SUCCESS;
}

/*
 * Add new device to the list of existing devices.
 */
error_t device_register(device_t *dev)
{
    error_t err;

    spinlock_acquire(&registered_devices_lock);

    err = device_check_name(dev);
    if (err)
        goto out;

    dev->vnode = NULL;
    llist_add(&registered_devices, &dev->this);
    err = E_SUCCESS;

out:
    spinlock_release(&registered_devices_lock);
    return err;
}

/*
 *
 */
static inline struct vnode *device_acquire_vnode(struct device *dev)
{
    bool new;
    struct stat *stat;

    dev->vnode = vnode_acquire(dev->vnode, &new);
    if (!new)
        return dev->vnode;

    dev->vnode->operations = &devtmpfs_vnode_ops;
    dev->vnode->pdata = dev;

    /*
     * TODO: Set vnode rdev.
     * TODO: Set vnode type (chardev, blockdev).
     * TODO: Set vnode uid/gid.
     */
    stat = &dev->vnode->stat;
    clock_get_time(&stat->st_mtim);
    stat->st_ctim = stat->st_mtim;
    stat->st_mode = S_IRWU | S_IRWG;
    stat->st_nlink = 1;

    return dev->vnode;
}

struct file *device_open(device_t *dev)
{
    struct vnode *vnode = device_acquire_vnode(dev);
    struct file *file = file_open(vnode, dev->fops);
    vnode_release(vnode);
    return file;
}

/*
 *
 */
struct device *device_find(const char *name)
{
    struct device *dev;
    bool found = false;

    locked_scope (&registered_devices_lock) {
        FOREACH_LLIST (node, &registered_devices) {
            dev = container_of(node, device_t, this);
            if (!strcmp(dev->name, name)) {
                found = true;
                break;
            }
        }
    }

    if (!found)
        return NULL;

    return dev;
}

/*
 * Forward open call to the device's driver's fops.
 */
static struct file *devtmpfs_vnode_open(struct vnode *vnode)
{
    struct device *dev = vnode->pdata;
    return file_open(vnode, dev->fops);
}

/*
 *
 */
static void devtmpfs_vnode_release(struct vnode *vnode)
{
    struct device *dev = vnode->pdata;
    dev->vnode = NULL;
}

/*
 * Operations on device vnodes.
 */
static vnode_ops_t devtmpfs_vnode_ops = {
    .open = devtmpfs_vnode_open,
    .release = devtmpfs_vnode_release,
};

/*
 * Placeholder fops structure used by the root of the devtmpfs.
 */
static struct file_operations devtmpfs_root_fops = {
};

/*
 * Remove a child inside a directory.
 *
 * This fs only lists existing devices, cannot add new ones.
 */
static vnode_t *
devtmpfs_root_vnode_create(vnode_t *node, const char *name, vnode_type type)
{
    UNUSED(node);
    UNUSED(name);
    UNUSED(type);

    return PTR_ERR(E_NOT_SUPPORTED);
}

/*
 * Add new child inside a directory.
 *
 * This fs only lists existing devices, cannot remove them.
 */
static error_t devtmpfs_root_vnode_remove(vnode_t *node, const char *child)
{
    UNUSED(node);
    UNUSED(child);

    return E_NOT_SUPPORTED;
}

/*
 *
 */
static error_t devtmpfs_root_vnode_getdents(vnode_t *vnode, off_t *offp,
                                            void *buf, size_t *sizep)
{
    struct posix_dent *dent = buf;
    struct device *dev;
    size_t size = 0;
    off_t off = 0;

    UNUSED(vnode);

    spinlock_acquire(&registered_devices_lock);

    FOREACH_LLIST_ENTRY(dev, &registered_devices, this)
    {
        reclen_t reclen;
        size_t name_size;

        /* log(n) ... */
        if (off < *offp) {
            off += 1;
            continue;
        }

        name_size = strlen(dev->name);
        reclen = sizeof(*dent) + name_size;
        reclen = align_up(reclen, alignof(struct posix_dent));
        if (size + reclen > *sizep)
            break;

        dent->d_ino = 0;
        dent->d_type = (char)VNODE_CHARDEVICE; // TODO: is_block_device()
        dent->d_reclen = reclen;
        memcpy(dent->d_name, dev->name, name_size);

        dent = (void *)dent + reclen;
        size += reclen;
        off += 1;
    }

    spinlock_release(&registered_devices_lock);

    *sizep = size;
    *offp = off;

    return 0;
}

/*
 *
 */
static vnode_t *
devtmpfs_root_vnode_lookup(vnode_t *node, const path_segment_t *child)
{
    device_t *dev;

    UNUSED(node);

    locked_scope (&registered_devices_lock) {
        FOREACH_LLIST (node, &registered_devices) {
            dev = container_of(node, device_t, this);
            if (path_segment_is(device_name(dev), child))
                break;
            dev = NULL;
        }
    }

    if (dev == NULL)
        return PTR_ERR(E_NODEV);

    return device_acquire_vnode(dev);
}

/*
 *
 */
static struct file *devtmpfs_root_vnode_open(struct vnode *vnode)
{
    return file_open(vnode, &devtmpfs_root_fops);
}

/*
 *
 */
static vnode_ops_t devtmpfs_root_vnode_ops = {
    .open = devtmpfs_root_vnode_open,
    .create = devtmpfs_root_vnode_create,
    .remove = devtmpfs_root_vnode_remove,
    .lookup = devtmpfs_root_vnode_lookup,
    .getdents = devtmpfs_root_vnode_getdents,
};

static vnode_t *devtmpfs_root(vfs_t *vfs)
{
    struct devtmpfs *devtmpfs = vfs->pdata;
    return vnode_acquire(&devtmpfs->root, NULL);
}

static void devtmpfs_delete(vfs_t *vfs)
{
    UNUSED(vfs);
}

static vfs_ops_t devtmpfs_vfs_ops = {
    .root = devtmpfs_root,
    .delete = devtmpfs_delete,
};

static vfs_t *devtmpfs_new(struct block_device *blkdev)
{
    struct devtmpfs *devtmpfs;
    struct vfs *vfs;

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
    vfs->blkdev = blkdev;

    devtmpfs->root.fs = vfs;
    devtmpfs->root.type = VNODE_DIRECTORY;
    devtmpfs->root.operations = &devtmpfs_root_vnode_ops;
    devtmpfs->root.refcount = 1; // Do not release it
    devtmpfs->root.pdata = devtmpfs;

    devtmpfs->root.stat.st_mode = S_IRWXU;
    devtmpfs->root.stat.st_mode |= S_IRGRP | S_IXGRP;
    devtmpfs->root.stat.st_mode |= S_IROTH | S_IXOTH;

    return vfs;
}

static error_t devtmpfs_mount(void)
{
    return vfs_mount("/dev", "devtmpfs", NULL);
}

DECLARE_FILESYSTEM(devtmpfs, devtmpfs_new);
DECLARE_INITCALL(INIT_NORMAL, devtmpfs_mount);
