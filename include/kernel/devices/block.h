#ifndef KERNEL_DEVICES_BLOCK_H
#define KERNEL_DEVICES_BLOCK_H

#include <kernel/device.h>
#include <kernel/spinlock.h>

#include <libalgo/queue.h>
#include <utils/container_of.h>

#include <sys/types.h>

struct block_device {
    struct device dev;
    blksize_t block_size;
    blkcnt_t block_count;
    const struct block_device_ops *ops;
};

static inline struct block_device *to_blkdev(struct device *dev)
{
    return container_of(dev, struct block_device, dev);
}

enum block_io_request_type {
    BLOCK_IO_REQUEST_READ,
    BLOCK_IO_REQUEST_WRITE,
};

struct block_io_request {
    enum block_io_request_type type;
    off_t offset;   /* Offset into the block device. */
    blkcnt_t count; /* Number of blocks to read/write. */
    void *buf;
};

struct block_device_ops {
    error_t (*request)(struct block_device *, struct block_io_request *);
};

error_t block_device_register(struct block_device *);

static inline size_t block_device_size(struct block_device *blkdev)
{
    return blkdev->block_count * blkdev->block_size;
}

/** Read a single block from a block device.
 *
 * The block must be released using @ref block_free().
 *
 * @param block_index Index of the block (not its offset !)
 *
 * @return A memory buffer containing the content of the block.
 */
void *block_read(struct block_device *, blkcnt_t block_index);

/** Release a block allocated using @ref block_read(). */
void block_free(struct block_device *blkdev, void *block);

#endif /* KERNEL_DEVICES_BLOCK_H */
