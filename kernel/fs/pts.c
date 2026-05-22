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
#include <kernel/semaphore.h>
#include <kernel/vfs.h>
#include <kernel/kmalloc.h>
#include <kernel/memory.h>
#include <kernel/syscalls.h>
#include <kernel/refcnt.h>
#include <kernel/device.h>

#include <utils/compiler.h>

#include <libalgo/ringbuffer.h>
#include <libalgo/linked_list.h>

#include <string.h>
#include <limits.h>
#include <sys/errno.h>
#include <sys/types.h>

#define PTY_BUFFER_SIZE PAGE_SIZE
#define PTS_FORMAT "pts%d"

/*
 * Pseudo terminal master multiplexer.
 */
static struct device ptmx;

struct pty_fs {
	struct vnode *root;
};

struct pty_end {
	struct vnode      *vnode;
	struct pty        *pty;
	struct ringbuffer  buffer;
	struct semaphore   buffer_lock;
	bool               closed;
} ALIGNED(CPU_CACHE_ALIGN);

struct pty {
	char           name[NAME_MAX];
	node_t         this; // used to list open ptys
	unsigned int   index;
	refcnt_t       refcount;
	struct pty_end master;
	struct pty_end slave;
};

/*
 * List of all the open ptys in the system.
 */
static DECLARE_LLIST(pty_list);
static DECLARE_MUTEX(pty_list_lock);

static atomic_t pty_last_index = { 0 };

static inline bool pty_end_is_master(const struct pty_end *end)
{
	return end == &end->pty->master;
}

/*
 * Initialize the content of a pty_end structure.
 */
static error_t pty_end_init(struct pty *pty, struct pty_end *end, struct vnode_operations *vnops)
{
	struct user_creds *creds;
	struct vnode *vnode;
	void *buffer;

	buffer = kmalloc(PTY_BUFFER_SIZE, KMALLOC_KERNEL);
	if (!buffer)
		return E_NOMEM;

	vnode = vnode_alloc();
	if (!vnode) {
		kfree(buffer);
		return E_NOMEM;
	}

	end->pty = pty;
	end->vnode = vnode;
	vnode->type = VNODE_CHARDEVICE;
	vnode->operations = vnops;
	vnode->pdata = end;
	INIT_MUTEX(end->buffer_lock);
	ringbuffer_init(&end->buffer, buffer, PTY_BUFFER_SIZE);

	creds = creds_get(current->process->creds);
	vnode_fill_stats(vnode, S_IRWA, creds);
	creds_put(creds);

	return E_SUCCESS;
}

/*
 * Release the content of a pty_end structure.
 */
static void pty_end_release(struct pty_end *end)
{
	end->closed = true;

	if (end->vnode) {
		vnode_release(end->vnode);
		end->vnode = NULL;
	}

	if (end->buffer.buf_start) {
		kfree(end->buffer.buf_start);
		end->buffer.buf_start = NULL;
	}
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
static void pty_destroy(struct pty *pty)
{
	pty_end_release(&pty->master);
	pty_end_release(&pty->slave);
	kfree(pty);
}

/*
 *
 */
static char pty_apply_line_discipline(struct pty_end *dst, char c)
{
	return c;
}

/*
 *
 */
static ssize_t pty_write(struct file *file, const char *buffer, size_t size)
{
	struct vnode *vnode = file->vnode;
	struct pty_end *end = vnode->pdata;
	struct pty *pty = end->pty;
	struct pty_end *other;

	other = pty_end_is_master(end) ? &pty->slave : &pty->master;
	semaphore_acquire(&other->buffer_lock);

	/*
	 * Apply line discipline and push characters into
	 * the other end's buffer.
	 */
	size = MIN(size, ringbuffer_available(&other->buffer));
	for (size_t i = 0; i < size; ++i) {
		char c;

		c = pty_apply_line_discipline(other, buffer[i]);
		if (c < 0)
			continue;
		ringbuffer_push(&other->buffer, (u8 *)&c, 1);
	}

	semaphore_release(&other->buffer_lock);
	return size;
}

/*
 *
 */
static ssize_t pty_read(struct file *file, char *buffer, size_t size)
{
	struct vnode *vnode = file->vnode;
	struct pty_end *end = vnode->pdata;
	struct pty_end *other;
	struct pty *pty = end->pty;
	size_t remaining;
	ssize_t ret;

	other = pty_end_is_master(end) ? &pty->slave : &pty->master;
	semaphore_acquire(&end->buffer_lock);

	remaining = ringbuffer_remaining(&end->buffer);
	if (remaining == 0) {
		if (other->closed) {
			ret = -E_PIPE;
			goto out;
		}
		ret = -E_WOULD_BLOCK; // TODO: Allow blocking reads
		goto out;
	}

	if (remaining > size)
		remaining = size;

	ret = ringbuffer_pop(&end->buffer, (u8 *)buffer, remaining);

out:
	semaphore_release(&end->buffer_lock);
	return ret;
}

static struct file_operations pty_fops = {
	.read = pty_read,
	.write = pty_write,
	.seek = default_file_seek,
};

/*
 *
 */
static struct file *pty_vnode_open(struct vnode *vnode)
{
	struct pty_end *end = vnode->pdata;

	/* master should not be accessible through the VFS */
	if (WARN_ON_MSG(pty_end_is_master(end), "trying to open master end of pty"))
		return PTR_ERR(E_NOT_SUPPORTED);

	return file_open(vnode, &pty_fops);
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
static void pty_vnode_release(struct vnode *vnode)
{
	struct pty_end *end = vnode->pdata;
	struct pty *pty = end->pty;

	/* avoid double free, vnode is released later in vnode_release(). */
	end->vnode = NULL;

	if (pty_end_is_master(end)) {
		/* no new slave after the master has been closed */
		semaphore_acquire(&pty_list_lock);
		llist_remove(&pty->this);
		semaphore_release(&pty_list_lock);
		/* release the 'phantom' reference (see function description) */
		vnode_release(pty->slave.vnode);
	}

	if (refcnt_put(&pty->refcount) == 0)
		pty_destroy(pty);
}

/*
 *
 */
static struct vnode *pty_vnode_lookup(struct vnode *root, const path_segment_t *name)
{
	struct vnode *vnode;
	struct pty *pty;
	bool found = false;

	UNUSED(root);

	semaphore_acquire(&pty_list_lock);
	FOREACH_LLIST_ENTRY(pty, &pty_list, this) {
		if (path_segment_is(pty->name, name)) {
			found = true;
			break;
		}
	}

	if (found)
		vnode = vnode_acquire(pty->slave.vnode, NULL);
	else
		vnode = PTR_ERR(E_NOENT);

	semaphore_release(&pty_list_lock);
	return vnode;
}

static struct vnode_operations pty_vnops = {
	.open = pty_vnode_open,
	.release = pty_vnode_release,
};

/*
 * PTY filesystem root directory.
 */
static struct vnode_operations pty_root_vnops = {
	.lookup = pty_vnode_lookup,
};

/*
 * Allocate a PTY filesystem.
 */
static struct pty_fs *ptyfs_alloc(void)
{
	return kcalloc(1, sizeof(struct pty_fs), KMALLOC_KERNEL);
}

/*
 * Destroy a PTY filesystem.
 */
static void ptyfs_destroy(struct pty_fs *ptyfs)
{
	if (ptyfs->root)
		vnode_release(ptyfs->root);
	kfree(ptyfs);
}

/*
 * Get the root of a PTY filesystem.
 */
static struct vnode *ptyfs_root(struct vfs *vfs)
{
	struct pty_fs *ptyfs = vfs->pdata;

	return ptyfs->root;
}

/*
 * Called when unmounting a PTY filesystem.
 */
static void ptyfs_delete(struct vfs *fs)
{
	ptyfs_destroy(fs->pdata);
}

static struct vfs_operations ptyfs_ops = {
	.root = ptyfs_root,
	.delete = ptyfs_delete,
};

/*
 * Allocate and initialize a PTY filesystem.
 */
struct vfs *ptyfs_new(struct block_device *blkdev)
{
	struct pty_fs *ptyfs = NULL;
	struct vnode *root = NULL;
	struct vfs *vfs = NULL;

	vfs = kcalloc(1, sizeof(*vfs), KMALLOC_KERNEL);
	if (!vfs)
		goto nomem;

	ptyfs = ptyfs_alloc();
	if (!ptyfs)
		goto nomem;

	root = vnode_alloc();
	if (!root)
		goto nomem;

	ptyfs->root = root;
	root->type = VNODE_DIRECTORY;
	root->operations = &pty_root_vnops;
	vnode_fill_stats(root, S_IRWA, &root_creds);

	vfs->pdata = ptyfs;
	vfs->blkdev = blkdev;
	vfs->operations = &ptyfs_ops;

	return vfs;

nomem:
	if (ptyfs)
		ptyfs_destroy(ptyfs);
	if (vfs)
		kfree(vfs);
	return PTR_ERR(E_NOMEM);
}

DECLARE_FILESYSTEM(pty, ptyfs_new);

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

	/* take extra reference and initialize list to be able
	 * to use pty_destroy() inside the error path. */
	refcnt_init(&pty->refcount);
	INIT_LLIST_NODE(pty->this);

	err = pty_end_init(pty, &pty->master, &pty_vnops);
	if (err)
		goto fail;
	refcnt_get(&pty->refcount);

	err = pty_end_init(pty, &pty->slave, &pty_vnops);
	if (err)
		goto fail;
	refcnt_get(&pty->refcount);

	/* switch file's vnode with that of the master's */
	vnode_release(file->vnode);
	file->vnode = pty->master.vnode;
	file->ops = &pty_fops;

	/* initialize PTY */
	pty->index = atomic_inc(&pty_last_index);
	snprintk(pty->name, sizeof(pty->name), PTS_FORMAT, pty->index);

	/* register PTY */
	semaphore_acquire(&pty_list_lock);
	llist_add(&pty_list, &pty->this);
	semaphore_release(&pty_list_lock);

	refcnt_put(&pty->refcount);

	return E_SUCCESS;

fail:
	pty_destroy(pty);
	return -err;
}

static struct file_operations ptmx_fops = {
	.open = ptmx_open,
};

/*
 * Create pseudo terminal multiplexer.
 */
static error_t ptmx_init(void)
{
	struct vnode *vnode;

	memset(&ptmx, 0, sizeof(ptmx));
	device_set_name(&ptmx, "ptmx");

	vnode = vnode_alloc();
	if (!vnode)
		return E_NOMEM;

	ptmx.vnode = vnode;
	ptmx.fops = &ptmx_fops;
	vnode->type = VNODE_CHARDEVICE;
	vnode_fill_stats(vnode, S_IRWA, &root_creds);

	return device_register(&ptmx);
}
DECLARE_INITCALL(INIT_NORMAL, ptmx_init);
