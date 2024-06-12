#include <kernel/error.h>
#include <kernel/logger.h>
#include <kernel/mmu.h>

#include <uacpi/event.h>
#include <uacpi/uacpi.h>
#include <uacpi/utilities.h>

#include <multiboot.h>

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

static uacpi_ns_iteration_decision
acpi_list_one_device(void *ctx, uacpi_namespace_node *node)
{
    uacpi_namespace_node_info *info;

    (void)ctx;

    uacpi_status ret = uacpi_get_namespace_node_info(node, &info);
    if (uacpi_unlikely_error(ret)) {
        const char *path = uacpi_namespace_node_generate_absolute_path(node);
        log_err("acpi", "failed to retrieve node %s information: %s", path,
                uacpi_status_to_string(ret));
        uacpi_free_absolute_path(path);
        return UACPI_NS_ITERATION_DECISION_CONTINUE;
    }

    if (info->type == UACPI_OBJECT_DEVICE) {
        log_info("acpi", "detected device: %s", info->name.text);
    }

    uacpi_free_namespace_node_info(info);

    return UACPI_NS_ITERATION_DECISION_CONTINUE;
}

// TODO: temporary, only for debug purposes until we implement a proper
//       automatic driver probing mechanism
void acpi_list_devices(void)
{
    uacpi_namespace_for_each_node_depth_first(uacpi_namespace_root(),
                                              acpi_list_one_device, UACPI_NULL);
}
