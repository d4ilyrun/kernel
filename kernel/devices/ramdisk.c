#include <string.h>
#define LOG_DOMAIN "ramdisk"

#include <kernel/devices/block.h>
#include <kernel/devices/ramdisk.h>
#include <kernel/file.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/memory.h>
#include <kernel/vm.h>

#include <utils/container_of.h>

struct ramdisk {
    struct block_device dev;
    struct vm_segment *segment;
};

static inline struct ramdisk *to_ramdisk(struct block_device *blkdev)
{
    return container_of(blkdev, struct ramdisk, dev);
}

static inline void
ramdisk_read(struct ramdisk *ramdisk, char *buffer, off_t off, size_t size)
{
    memcpy(buffer, (void *)ramdisk->segment->start + off, size);
}

static inline void ramdisk_write(struct ramdisk *ramdisk, const char *buffer,
                                 off_t off, size_t size)
{
    memcpy((void *)ramdisk->segment->start + off, buffer, size);
}

static ssize_t ramdisk_file_read(struct file *file, char *buffer, size_t size)
{
    struct ramdisk *ramdisk = to_ramdisk(file->priv);

    if (size > ramdisk->segment->size) {
        log_warn("%s: out of bounds read", ramdisk->dev.dev.name);
        return -E_INVAL;
    }

    ramdisk_read(ramdisk, buffer, file->pos, size);

    return size;
}

static size_t ramdisk_file_size(struct file *file)
{
    struct ramdisk *ramdisk = to_ramdisk(file->priv);

    return ramdisk->segment->size;
}

/*
 * Read-only file.
 */
static const struct file_operations ramdisk_device_fops = {
    .read = ramdisk_file_read,
    .size = ramdisk_file_size,
    .seek = default_file_seek,
};

static error_t ramdisk_block_request(struct block_device *blkdev,
                                     struct block_io_request *request)
{
    struct ramdisk *ramdisk = to_ramdisk(blkdev);

    switch (request->type) {
    case BLOCK_IO_REQUEST_READ:
        ramdisk_read(ramdisk, request->buf, request->offset,
                     request->count * PAGE_SIZE);
        break;
    case BLOCK_IO_REQUEST_WRITE:
        ramdisk_write(ramdisk, request->buf, request->offset,
                      request->count * PAGE_SIZE);
        break;
    }

    return E_SUCCESS;
}

static const struct block_device_ops ramdisk_block_ops = {
    .request = ramdisk_block_request,
};

struct device *ramdisk_create(const char *name, paddr_t start, size_t size)
{
    struct ramdisk *ramdisk;
    struct block_device *blkdev;
    struct vm_segment *vm_segment;
    void *vaddr;
    error_t err;

    if (!PAGE_ALIGNED(size)) {
        log_err("%s: size is not page aligned", name);
        return PTR_ERR(E_INVAL);
    }

    vaddr = vm_alloc_at(&kernel_address_space, start, size, VM_KERNEL_RW);
    if (!vaddr) {
        log_err("%s: failed to allocate vm_segment", name);
        return PTR_ERR(E_NOMEM);
    }

    vm_segment = vm_find(&kernel_address_space, vaddr);
    if (!vm_segment)
        PANIC("kernel_address_space corrupted ?");

    ramdisk = kcalloc(1, sizeof(*ramdisk), KMALLOC_KERNEL);
    if (!ramdisk) {
        log_err("%s: failed to allocate struct", name);
        err = E_NOMEM;
        goto ramdisk_create_fail;
    }

    ramdisk->segment = vm_segment;

    blkdev = &ramdisk->dev;
    blkdev->ops = &ramdisk_block_ops;
    blkdev->dev.fops = &ramdisk_device_fops;
    blkdev->block_size = PAGE_SIZE;
    blkdev->block_count = size / PAGE_SIZE;
    device_set_name(&blkdev->dev, name);

    err = block_device_register(&ramdisk->dev);
    if (err) {
        log_err("failed to register block device: %d", err);
        goto ramdisk_create_fail;
    }

    return &ramdisk->dev.dev;

ramdisk_create_fail:
    kfree(ramdisk);
    vm_free(&kernel_address_space, vm_segment);
    return PTR_ERR(err);
}
