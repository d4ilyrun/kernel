#define LOG_DOMAIN "block"

#include <kernel/devices/block.h>
#include <kernel/file.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/memory.h>
#include <kernel/vfs.h>

#include <utils/constants.h>

static inline ssize_t
block_device_request(struct block_device *blkdev, void *buf, blkcnt_t count,
                     off_t offset, enum block_io_request_type type)
{
    struct block_io_request request;
    blkcnt_t start_block;
    error_t err;

    if (offset % blkdev->block_size)
        return -E_INVAL;

    /*
     * Request does not fit inside the device's addressable blocks.
     */
    start_block = offset / blkdev->block_size;
    if (start_block > blkdev->block_count ||
        count > (blkdev->block_count - start_block)) {
        return -E_INVAL;
    }

    request.type = type;
    request.buf = buf;
    request.count = count;
    request.offset = offset;

    err = blkdev->ops->request(blkdev, &request);
    if (err)
        return -err;

    return count * (ssize_t)blkdev->block_size;
}

static ssize_t block_device_read(struct file *file, char *out, size_t size)
{
    struct block_device *blkdev = file->vnode->pdata;
    ssize_t read;

    if (size % blkdev->block_size)
        return -E_INVAL;

    /* TODO: Read from page cache */
    read = block_device_request(blkdev, out, size / blkdev->block_size,
                                file->pos, BLOCK_IO_REQUEST_READ);
    if (read < 0)
        return read;

    file->pos += size;

    return read;
}

static ssize_t
block_device_write(struct file *file, const char *in, size_t size)
{
    struct block_device *blkdev = file->vnode->pdata;
    ssize_t written;

    if (size % blkdev->block_size)
        return -E_INVAL;

    /* TODO: Writeback mechanism */
    written = block_device_request(blkdev, (char *)in,
                                   size / blkdev->block_size, file->pos,
                                   BLOCK_IO_REQUEST_WRITE);
    if (written < 0)
        return written;

    file->pos += size;

    return written;
}

static error_t block_device_open(struct file *file)
{
    struct device *dev = file->vnode->pdata;
    struct block_device *blkdev = to_blkdev(dev);

    file->vnode->stat.st_blksize = blkdev->block_size;
    file->vnode->stat.st_blocks = blkdev->block_count;

    return E_SUCCESS;
}

static const struct file_operations block_device_fops = {
    .open = block_device_open,
    .read = block_device_read,
    .write = block_device_write,
    .seek = default_file_seek,
};

error_t block_device_register(struct block_device *blkdev)
{
    error_t err;

    blkdev->dev.fops = &block_device_fops;

    if (!blkdev->ops->request)
        return E_INVAL;

    err = block_device_cache_init(blkdev);
    if (err)
        return err;

    log_info("%s: new block device (size: %ldMB, block_size: %ldB)",
             device_name(&blkdev->dev),
             (blkdev->block_size * blkdev->block_count) / MB,
             blkdev->block_size);

    return device_register(&blkdev->dev);
}

error_t
block_read_direct(struct block_device *blkdev, void *buffer, blkcnt_t block)
{
    ssize_t size;
    error_t err;

    size = block_device_request(blkdev, buffer, 1, block * blkdev->block_size,
                                BLOCK_IO_REQUEST_READ);
    if (size < 0) {
        err = -size;
        log_err("%s: failed to read block %ld: %pe", blkdev->dev.name, block,
                &err);
        return err;
    }

    WARN_ON(size != blkdev->block_size);

    return E_SUCCESS;
}

error_t block_write_direct(struct block_device *blkdev, const void *buffer,
                           blkcnt_t block)
{
    ssize_t size;
    error_t err;

    size = block_device_request(blkdev, (void *)buffer, 1,
                                block * blkdev->block_size,
                                BLOCK_IO_REQUEST_WRITE);
    if (size < 0) {
        err = -size;
        log_err("%s: failed to write block %ld: %pe", blkdev->dev.name, block,
                &err);
        return err;
    }

    WARN_ON(size != blkdev->block_size);

    return E_SUCCESS;
}

void *const *block_get(struct block_device *blkdev, blkcnt_t block)
{
    const struct page_cache_entry *entry;

    entry = block_device_cache_get(blkdev, block);
    if (IS_ERR(entry))
        return (void *)entry;

    return &entry->buffer;
}

void block_release(struct block_device *blkdev, void *const *pbuffer)
{
    struct page_cache_entry *entry;

    entry = container_of(pbuffer, struct page_cache_entry, buffer);
    block_device_cache_put(blkdev, entry);
}
