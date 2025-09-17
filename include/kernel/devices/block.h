/**
 * @defgroup kernel_devices_block Block devices
 * @ingroup kernel_devices
 *
 * # Block devices
 *
 * ## Caching
 *
 * To avoid having to write to the disk everytime we write a to block device
 * these operations instead go through a caching system.
 *
 * Each block devices maintains a cache of memory pages that correspond
 * to 1 physical blocks each (or multiple depending on the block size).
 * When writing reading/writing a block the operations is performed on
 * the memory page instead, and a kernel thread regularily flushes newly
 * modified pages to disk.
 *
 * When multiple users want to interact with a same physical block the cache,
 * they all have to go through this cache and end up sharing the same page.
 *
 * TODO: Create a new page CoW page for private mappings.
 *
 * In the original Unix design, per-block accesses (\c read/write) and page
 * wide accesses (\c mmap) both used different caches (respectively called
 * buffer cache and page cache). Our design merges both of those into a single
 * page cache that can contain multiple blocks per page.
 *
 * @{
 */

#ifndef KERNEL_DEVICES_BLOCK_H
#define KERNEL_DEVICES_BLOCK_H

#include <kernel/device.h>
#include <kernel/spinlock.h>

#include <libalgo/queue.h>
#include <utils/container_of.h>

#include <sys/types.h>

/**
 * Per-device page cache.
 */
struct page_cache {
    llist_t entries;              /*!< Cache entries */
    spinlock_t lock;              /*!< Cache wide lock */
    unsigned int blocks_per_page; /*!< Number of blocks per memory page */
};

/**
 * Single entry inside a block device's page cache.
 *
 * The entry is reference counted and must be obtained/released using
 * block_device_cache_get()/block_device_cache_put().
 */
struct page_cache_entry {
    node_t this;              /*!< Used by @c cache->entries */
    node_t this_global;       /*!< Used by the global list of cached pages */
    blkcnt_t first_block;     /*!< Index of the first block inside this page */
    struct page_cache *cache; /*!< The cache this entry is located inside */
    struct page *page;        /*!< This entry's physical page */
    void *buffer;             /*!< Kernel buffer mapped onto @c page */
    unsigned int refcount;    /*!< Reference count */
};

/**
 * Block device
 */
struct block_device {
    struct device dev;
    blksize_t block_size; /*!< The size of a single physical block */
    blkcnt_t block_count; /*!< The number of addressable blocks on the device */
    struct page_cache cache; /*!< This block device's page cache */
    const struct block_device_ops *ops;
};

static inline struct block_device *to_blkdev(struct device *dev)
{
    return container_of(dev, struct block_device, dev);
}

/** */
enum block_io_request_type {
    BLOCK_IO_REQUEST_READ,  /*!< READ request */
    BLOCK_IO_REQUEST_WRITE, /*!< WRITE request */
};

/** */
struct block_io_request {
    enum block_io_request_type type; /*!< @ref block_io_request_type */
    off_t offset;                    /*!< Offset into the block device. */
    blkcnt_t count;                  /*!< Number of blocks to read/write. */
    void *buf;
};

/**
 * Block device operations.
 */
struct block_device_ops {
    error_t (*request)(struct block_device *, struct block_io_request *);
};

error_t block_device_register(struct block_device *);

static inline size_t block_device_size(struct block_device *blkdev)
{
    return blkdev->block_count * blkdev->block_size;
}

/** Initialize a block device's page cache. */
error_t block_device_cache_init(struct block_device *);

/** Retreive the cached page which contains the block at offset @c offset.
 *
 *  If no cached page exists for this block this function creates a new one,
 *  fills it, and returns it.
 *
 *  @param offset Offset inside the block device (in terms of bytes)
 */
const struct page_cache_entry *
block_device_cache_get(struct block_device *, off_t);

/** Remove an entry from a block device's page cache.
 *
 *  The entry being reference counted, it is only removed once all references
 *  to it (acquired through \c block_device_cache_get()) are released.
 *
 *  @return \c true if the entry was effectively removed from the cache.
 */
bool block_device_cache_put(struct block_device *, struct page_cache_entry *);

/** Write the content of the cached page to the device. */
error_t block_device_cache_writeback(struct page_cache_entry *entry);

/** Read the content of a block into a buffer.
 *  The buffer must be at least blkdev->block_size bytes large.
 *
 *  @param block Index of the block.
 */
error_t block_read_direct(struct block_device *, void *, blkcnt_t block);

/** Write the content of buffer to a physical block.
 *  The buffer must be at least blkdev->block_size bytes large.
 *
 *  @param block Index of the block.
 */
error_t block_write_direct(struct block_device *, const void *, blkcnt_t block);

/** Read a single block from a block device.
 *
 * The block must be released using @ref block_release().
 *
 * @param block_index Index of the block (not its offset !)
 *
 * @return A pointer to the memory buffer containing the content of the block.
 */
void *const *block_get(struct block_device *, blkcnt_t block_index);

/** Release a block obtained through @ref block_get(). */
void block_release(struct block_device *blkdev, void *const *block);

#endif /* KERNEL_DEVICES_BLOCK_H */

/** @} */
