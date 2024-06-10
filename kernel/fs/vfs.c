#include <kernel/error.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/vfs.h>

#include <utils/container_of.h>

#include <string.h>

DECLARE_LLIST(vfs_mountpoints);

// Defined inside our linkerscript
// These symbols are placed around the '.data.vfs.filesystems' section
extern u32 _kernel_filesystems_start;
extern u32 _kernel_filesystems_end;

static const vfs_fs_t *kernel_filesystems_start =
    (const vfs_fs_t *)&_kernel_filesystems_start;

static const vfs_fs_t *kernel_filesystems_end =
    (const vfs_fs_t *)&_kernel_filesystems_end;

static const vfs_fs_t *vfs_find_fs(const char *fs_type)
{
    for (const vfs_fs_t *fs = kernel_filesystems_start;
         fs < kernel_filesystems_end; ++fs) {
        if (!strcmp(fs->name, fs_type))
            return fs;
    }

    return NULL;
}

static vfs_t *vfs_root_fs()
{
    if (llist_is_empty(vfs_mountpoints))
        return NULL;
    return container_of(llist_head(vfs_mountpoints), vfs_t, this);
}

error_t vfs_mount_root(const char *fs_type, dev_t *dev)
{
    // Mounting a new root is not supported for the time being
    if (!llist_is_empty(vfs_mountpoints))
        return E_INVAL;

    const vfs_fs_t *fs = vfs_find_fs(fs_type);
    if (fs == NULL)
        return E_INVAL;

    vfs_t *new = fs->new (dev);
    if (IS_ERR(new))
        return ERR_FROM_PTR(new);

    // root fs points to no existing vnode
    new->node = NULL;

    llist_add(&vfs_mountpoints, &new->this);
    return E_SUCCESS;
}

vnode_t *vfs_find_by_path(const char *raw_path)
{
    path_t path = NEW_DYNAMIC_PATH(raw_path);

    vfs_t *root_fs = vfs_root_fs();
    if (root_fs == NULL)
        return PTR_ERR(E_NOENT);

    vnode_t *current = root_fs->operations->root(root_fs);
    DO_FOREACH_SEGMENT(segment, &path, {
        vnode_t *old = current;
        current = current->operations->lookup(current, &segment);
        // NOTE: vfs_vnode_acquire is called inside the `lookup` function in
        //       order to return the new vnoden this is why we must release
        //       this vnode. This looks a bit weird, or 'hidden', and should
        //       maybe be refactored.
        vfs_vnode_release(old);
        if (IS_ERR(current))
            break;
    });

    return current;
}

vnode_t *vfs_vnode_acquire(vnode_t *node, bool *new)
{
    if (node == NULL) {
        node = kcalloc(1, sizeof(vnode_t), KMALLOC_KERNEL);
        if (node == NULL)
            return PTR_ERR(E_NOMEM);
        if (new)
            *new = true;
    } else {
        if (new)
            *new = false;
    }

    node->refcount += 1;

    return node;
}

vnode_t *vfs_vnode_release(vnode_t *node)
{
    if (node->refcount <= 1) {
        if (node->operations->release)
            node->operations->release(node);
        kfree(node);
        return NULL;
    }

    node->refcount -= 1;
    return node;
}
