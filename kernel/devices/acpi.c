#include <kernel/devices/acpi.h>
#include <kernel/devices/driver.h>
#include <kernel/error.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/mmu.h>

#include <uacpi/event.h>
#include <uacpi/uacpi.h>
#include <uacpi/utilities.h>
#include <utils/macro.h>

#include <multiboot.h>
#include <string.h>

static inline struct acpi_driver *to_acpi_drv(driver_t *drv)
{
    return (struct acpi_driver *)drv;
}

static inline struct acpi_device *to_acpi_dev(device_t *dev)
{
    return (struct acpi_device *)dev;
}

error_t acpi_init(struct multiboot_info *mbt)
{
    struct multiboot_tag_new_acpi *acpi_tag = NULL;

    // RSDP should be passed on by our multiboot compliant bootloader
    FOREACH_MULTIBOOT_TAG (tag, mbt) {
        if (tag->type == MULTIBOOT_TAG_TYPE_ACPI_NEW ||
            tag->type == MULTIBOOT_TAG_TYPE_ACPI_OLD) {
            acpi_tag = (void *)tag;
            break;
        }
    }

    if (acpi_tag == NULL) {
        log_err("acpi", "No ACPI tag present inside the multiboot structure");
        return E_INVAL;
    }

    uacpi_status ret;
    uacpi_init_params init_params = {
        .rsdp = mmu_find_physical((vaddr_t)&acpi_tag->rsdp),
        .log_level = UACPI_LOG_INFO,
        .flags = 0,
    };

    ret = uacpi_initialize(&init_params);
    if (uacpi_unlikely_error(ret)) {
        log_err("acpi", "Failed to initialize uACPI: %s",
                uacpi_status_to_string(ret));
        return E_INVAL;
    }

    ret = uacpi_namespace_load();
    if (uacpi_unlikely_error(ret)) {
        log_err("acpi", "Failed to load AML namespace: %s",
                uacpi_status_to_string(ret));
        return E_INVAL;
    }

    ret = uacpi_namespace_initialize();
    if (uacpi_unlikely_error(ret)) {
        log_err("acpi", "Failed to initialize AML namespace: %s",
                uacpi_status_to_string(ret));
        return E_INVAL;
    }

    ret = uacpi_finalize_gpe_initialization();
    if (uacpi_unlikely_error(ret)) {
        log_err("acpi", "Failed to finalize GPE initialization: %s",
                uacpi_status_to_string(ret));
        return E_INVAL;
    }

    return E_SUCCESS;
}

static driver_t *acpi_driver_find_by_id(const char *id)
{
    struct acpi_device dev;
    strlcpy(dev.id, id, ACPI_ID_MAX_LEN);
    return driver_find_match(&dev.device);
}

static error_t acpi_device_probe_with_id(driver_t *drv, const char *id)
{
    struct acpi_device *dev;
    error_t ret;

    dev = kmalloc(sizeof(*dev), KMALLOC_KERNEL);
    if (dev == NULL)
        return E_NOMEM;

    strlcpy(dev->id, id, ACPI_ID_MAX_LEN);
    dev->device.driver = drv;

    ret = driver_probe(drv, &dev->device);
    if (ret)
        kfree(dev);

    return ret;
}

static uacpi_ns_iteration_decision
acpi_start_one_device(void *ctx, uacpi_namespace_node *node)
{
    UNUSED(ctx);

    uacpi_namespace_node_info *info;
    driver_t *driver = NULL;
    const char *id;

    uacpi_status ret = uacpi_get_namespace_node_info(node, &info);
    if (uacpi_unlikely_error(ret)) {
        const char *path = uacpi_namespace_node_generate_absolute_path(node);
        log_err("acpi", "failed to retrieve node %s information: %s", path,
                uacpi_status_to_string(ret));
        goto out;
    }

    if (info->type == UACPI_OBJECT_DEVICE) {
        if (info->flags & UACPI_NS_NODE_INFO_HAS_HID) {
            driver = acpi_driver_find_by_id(info->hid.value);
            if (driver != NULL)
                id = info->hid.value;
        }

        if (driver == NULL && (info->flags & UACPI_NS_NODE_INFO_HAS_CID)) {
            for (u32 i = 0; i < info->cid.num_ids; ++i) {
                driver = acpi_driver_find_by_id(info->cid.ids[i].value);
                if (driver != NULL) {
                    id = info->cid.ids[i].value;
                    break;
                }
            }
        }
    }

    if (driver != NULL)
        acpi_device_probe_with_id(driver, id);

out:
    uacpi_free_namespace_node_info(info);
    return UACPI_NS_ITERATION_DECISION_CONTINUE;
}

void acpi_probe_devices(void)
{
    uacpi_namespace_for_each_node_depth_first(
        uacpi_namespace_root(), acpi_start_one_device, UACPI_NULL);
}

static bool acpi_driver_match_device(const driver_t *drv, const device_t *dev)
{
    struct acpi_driver *acpi_drv = to_acpi_drv((driver_t *)drv);
    struct acpi_device *acpi_dev = to_acpi_dev((device_t *)dev);

    return !strcmp(acpi_dev->id, acpi_drv->compatible);
}

void acpi_driver_register(struct acpi_driver *driver)
{
    driver->driver.operations.match = acpi_driver_match_device;
    driver_register(&driver->driver);
}
