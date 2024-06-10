#include <kernel/device.h>
#include <kernel/kmalloc.h>

#include <string.h>

static error_t device_read(const dev_t *dev, char *buffer, size_t offset,
                           size_t size)
{
    if (offset + size > dev->size)
        return -E_INVAL;

    memcpy(buffer, (void *)dev->start + offset, size);
    return E_SUCCESS;
}

static error_t device_write(const dev_t *dev, size_t offset, const char *buffer,
                            size_t size)
{
    if (offset + size > dev->size)
        return -E_INVAL;

    memcpy((void *)dev->start + offset, buffer, size);
    return E_SUCCESS;
}

dev_t *device_new(u32 start, size_t size)
{
    dev_t *dev = kcalloc(1, sizeof(dev_t), KMALLOC_KERNEL);
    if (dev == NULL)
        return PTR_ERR(E_NOT_IMPLEMENTED);

    *dev = (dev_t){
        .start = start,
        .size = size,
        .operations =
            {
                .read = device_read,
                .write = device_write,
            },
    };

    return dev;
}
