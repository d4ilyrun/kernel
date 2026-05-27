/*
 * Pseudo terminal interface (POSIX.1-2024)
 *
 * ## Allocating a master/slave pair
 *
 * Pseudo terminal pairs are allocated by the multiplexer device.
 *
 * We are doing this because posix_openpt() is similar in many ways to
 * the regular open(). To avoid duplicating the code we use a device
 * that allocates a pty pair and returns the slave's vnode.
 */

#include <kernel/init.h>
#include <kernel/spinlock.h>
#include <kernel/vfs.h>
#include <kernel/kmalloc.h>
#include <kernel/memory.h>
#include <kernel/syscalls.h>
#include <kernel/refcnt.h>

#include <utils/compiler.h>
#include <utils/compiler.h>

#include <libalgo/ringbuffer.h>
#include <libalgo/linked_list.h>

#include <string.h>
#include <limits.h>
#include <sys/errno.h>
#include <sys/types.h>
#include "kernel/error.h"
#include "kernel/logger.h"

#define PTY_BUFFER_SIZE PAGE_SIZE
#define PTS_FORMAT "pts%d"

struct pty_end {
    struct vnode      *vnode;
    struct pty        *pty;
    struct ringbuffer  buffer;
    spinlock_t         buffer_lock;
} ALIGNED(CPU_CACHE_ALIGN);

struct pty {
    node_t         this; // used by the global list of open ptys
    unsigned int   index;
    refcnt_t       refcount;
    struct pty_end master;
    struct pty_end slave;
};

/*
 * List of all the open ptys in the system.
 */
static DECLARE_LLIST(pty_list);
static DECLARE_SPINLOCK(pty_list_lock);

static atomic_t pty_last_index = { 0 };

static inline bool pty_end_is_master(const struct pty_end *end)
{
    return end == &end->pty->master;
}

/*
 * Initialize the content of a pty_end structure.
 */
static error_t pty_end_init(struct pty_end *end,
                            struct vnode_operations *vnops)
{
    struct vnode *vnode;
    void *buffer;

    buffer = kmalloc(PTY_BUFFER_SIZE, KMALLOC_KERNEL);
    if (!buffer)
        return E_NOMEM;

    vnode = vnode_new();
    if (IS_ERR(vnode)) {
        kfree(buffer);
        return ERR_FROM_PTR(vnode);
    }

    end->vnode = vnode;
    vnode->operations = vnops;
    vnode->pdata = end;

    vnode->type = VNODE_CHARDEVICE;
    vnode->stat.st_nlink = 1;
    vnode->stat.st_uid = UID_ROOT;
    vnode->stat.st_gid = GID_ROOT;

    ringbuffer_init(&end->buffer, buffer, PTY_BUFFER_SIZE);

    return E_SUCCESS;
}

/*
 * Release the content of a pty_end structure.
 */
static void pty_end_release(struct pty_end *end)
{
    vnode_release(end->vnode);
    end->vnode = NULL; /* allow freeing in error path */
    kfree(end->buffer.buf_start);
    end->buffer.buf_start = NULL;  /* allow freeing in error path */
}

/*
 *
 */
static struct pty *pty_alloc(void)
{
    return kcalloc(1, sizeof(struct pty), KMALLOC_KERNEL);

}

/*
 *
 */
static void pty_release(struct pty *pty)
{
    pty_end_release(&pty->master);
    pty_end_release(&pty->slave);
    kfree(pty);
}

/*
 * Apply the TTY's current line discipline rul
 */
static void tty_process_and_write(struct pty_end *dst, char c)
{
    WARN_ON(!spinlock_is_held(&dst->buffer_lock));

    if (ringbuffer_available(&dst->buffer) == 0) {
        // TODO: Blocking I/O
        log_warn("ringbuffer full, skipping character");
        return;
    }

    /* no actual line control performed for the time being */
    ringbuffer_push(&dst->buffer, (void *)&c, sizeof(c));
}

/*
 *
 */
static ssize_t tty_write(struct file *file, const char *buffer, size_t size)
{
    struct vnode *vnode = file->vnode;
    struct pty_end *end = vnode->pdata;
    struct pty *pty = end->pty;
    struct pty_end *other;

    pty = end->pty;
    other = pty_end_is_master(end) ? &pty->slave : &pty->master;

    spinlock_acquire(&other->buffer_lock);
    for (size_t i = 0; i < size; ++i)
        tty_process_and_write(other, buffer[i]);
    spinlock_release(&other->buffer_lock);

    return size;
}

/*
 *
 */
static ssize_t tty_read(struct file *file, char *buffer, size_t size)
{
    struct vnode *vnode = file->vnode;
    struct pty_end *end = vnode->pdata;
    size_t remaining;
    ssize_t ret;

    spinlock_acquire(&end->buffer_lock);

    remaining = ringbuffer_remaining(&end->buffer);
    if (remaining < size) {
        ret = -EWOULDBLOCK; // FIXME: PTY does not allow non-blocking reads
        goto out;
    }

    if (remaining > size)
        remaining = size;

    ret = ringbuffer_pop(&end->buffer, (u8 *)buffer, remaining);

out:
    spinlock_release(&end->buffer_lock);
    return ret;
}

static struct file_operations tty_fops = {
    .read = tty_read,
    .write = tty_write,
    .seek = default_file_seek,
};

/*
 *
 */
static struct file *tty_open(struct vnode *vnode)
{
    struct pty_end *end = vnode->pdata;

    /* master should not be accessible through the VFS */
    if (WARN_ON_MSG(pty_end_is_master(end), "trying to open master end of pty"))
        return PTR_ERR(E_NOT_SUPPORTED);

    return file_open(vnode, &tty_fops);
}

/*
 * Close the end of a pseudo terminal pair.
 *
 * NOTE: A 'phantom' reference to the slave's vnode is taken in ptmx_open().
 *       This reference is only released when releasing the master vnode. This
 *       guarantees that the slave vnode is ALWAYS released after the master,
 *       since new slaves can be opened as long as an entry is present in
 *       /dev/pts.
 *
 * TODO: When closing the last file descriptor of the terminal end of a pty,
 *       send a SIGHUP to the controlling process (POSIX.1-2024).
 */
static void tty_release(struct vnode *vnode)
{
    struct pty_end *end = vnode->pdata;
    struct pty *pty = end->pty;

    /* avoid double free, vnode is released later in vnode_release(). */
    end->vnode = NULL;

    if (pty_end_is_master(end)) {
        /* no new slave after the master has been closed */
        spinlock_acquire(&pty_list_lock);
        llist_remove(&pty->this);
        spinlock_release(&pty_list_lock);
        /* release the 'phantom' reference (see function description) */
        vnode_release(pty->slave.vnode);
    }

    if (refcnt_put(&pty->refcount) == 0)
        pty_release(pty);
}

/*
 *
 */
static struct vnode *tty_lookup(struct vnode *root, const path_segment_t *name)
{
    struct pty *pty;
    char pts[NAME_MAX];
    bool found = false;

    UNUSED(root);

    spinlock_acquire(&pty_list_lock);
    FOREACH_LLIST_ENTRY(pty, &pty_list, this) {
        snprintk(pts, sizeof(pts), PTS_FORMAT, pty->index);
        if (path_segment_is(pts, name)) {
            found = true;
            break;
        }
    }
    spinlock_release(&pty_list_lock);

    if (!found)
        return PTR_ERR(E_NOENT);

    return vnode_acquire(pty->slave.vnode, NULL);
}

static struct vnode_operations tty_vnops = {
    .open = tty_open,
    .release = tty_release,
};

static struct vnode_operations tty_root_vnops = {
    .lookup = tty_lookup,
};

/*
 *
 */
static error_t ptmx_open(struct file *file)
{
    struct pty *pty;
    error_t err;

    pty = pty_alloc();
    if (!pty)
        return -E_NOMEM;

    err = pty_end_init(&pty->master, &tty_vnops);
    if (err)
        goto fail;

    err = pty_end_init(&pty->slave, &tty_vnops);
    if (err)
        goto fail;

    /* replace the file's vnode with that of the master's end */
    vnode_release(file->vnode);
    file->vnode = pty->master.vnode;

    refcnt_init(&pty->refcount);
    pty->index = atomic_inc(&pty_last_index);
    locked_scope (&pty_list_lock) {
        llist_add(&pty_list, &pty->this);
    }

    return E_SUCCESS;

fail:
    pty_release(pty);
    return -err;
}

/*
 * Pseudo terminal master multiplexer.
 */
static struct file_operations ptmx_fops = {
    .open = ptmx_open,
};

/*
 *
 */
struct vfs *tty_fs_new(struct block_device *blkdev)
{
    UNUSED(blkdev);
}

DECLARE_FILESYSTEM(tty, tty_fs_new);
