#define LOG_DOMAIN "block"

#include <kernel/devices/block.h>
#include <kernel/file.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
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

    if (size % blkdev->block_size)
        return -E_INVAL;

    /* TODO: Read from page cache */
    return block_device_request(blkdev, out, size / blkdev->block_size,
                                file->pos, BLOCK_IO_REQUEST_READ);
}

static ssize_t
block_device_write(struct file *file, const char *in, size_t size)
{
    struct block_device *blkdev = file->vnode->pdata;

    if (size % blkdev->block_size)
        return -E_INVAL;

    /* TODO: Writeback mechanism */
    return block_device_request(blkdev, (char *)in, size / blkdev->block_size,
                                file->pos, BLOCK_IO_REQUEST_WRITE);
}

static const struct file_operations block_device_fops = {
    .read = block_device_read,
    .write = block_device_write,
};

error_t block_device_register(struct block_device *blkdev)
{
    blkdev->dev.fops = &block_device_fops;

    if (!blkdev->ops->request)
        return E_INVAL;

    log_info("%s: new block device (size: %ldMB, block_size: %ldB)",
             device_name(&blkdev->dev),
             (blkdev->block_size * blkdev->block_count) / MB,
             blkdev->block_size);

    return device_register(&blkdev->dev);
}
