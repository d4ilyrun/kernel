#include <kernel/devices/acpi.h>
#include <kernel/devices/pci.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>

#include <utils/bits.h>
#include <utils/container_of.h>
#include <utils/macro.h>

#include "kernel/arch/i686/devices/pic.h"
#include "kernel/interrupts.h"

#define PCI_MAX_BUS 256
#define PCI_MAX_DEVICE 32

/* 6.2.5.1 */
#define PCI_BAR_MEMORY_SPACE_MASK 0x1
#define PCI_BAR_MEMORY_ADDRESS_MASK 0xfffffff0
#define PCI_BAR_MEMORY_TYPE_MASK 0x6
#define PCI_BAR_MEMORY_PREFETCH_MASK 0x8
#define PCI_BAR_IO_ADDRESS_MASK 0xfffffffc

typedef enum {
    PCI_BAR_MEMORY_32B = 0,
    PCI_BAR_MEMORY_64B = 2,
} pci_bar_memory_type;

/** The different types of PCI header layouts
 *  @see PCI specification 6-1
 */
typedef enum {
    PCI_HEADER_TYPE_GENERAL = 0x0,
    PCI_HEADER_TYPE_PCI_BRIDGE = 0x1,
    PCI_HEADER_TYPE_CARDBUS_BRIDGE = 0x2,
    /*
     * Can be present additionnaly to the other types if there is multiple host
     * controllers
     */
    PCI_HEADER_TYPE_MULTI_FUNCTION = 0x80,
} pci_header_type;

#define PCI_HEADER_TYPE_MASK (~PCI_HEADER_TYPE_MULTI_FUNCTION)

static DECLARE_LLIST(pci_registered_buses);

static bool pci_driver_match(const driver_t *drv, const device_t *dev)
{
    const struct pci_driver *pci_drv = to_pci_drv(drv);
    const struct pci_device *pci_dev = to_pci_dev(dev);

    return (pci_drv->compatible.vendor == pci_dev->id.vendor) &&
           (pci_drv->compatible.device == pci_dev->id.device);
}

void pci_driver_register(struct pci_driver *driver)
{
    driver->driver.operations.match = pci_driver_match;
    return driver_register(&driver->driver);
}

#define pci_device_read_header(_device, _header) \
    pci_read_header(_device->bus->number, _device->number, _header)

#define pci_device_write_header(_device, _header, _val) \
    pci_write_header(_device->bus->number, _device->number, _header, _val)

static inline struct pci_device_id
pci_read_header_id(uint8_t bus, uint8_t device)
{
    uint32_t id = pci_read_header(bus, device, ID);
    return *(struct pci_device_id *)&id;
}

void pci_device_enable_io(struct pci_device *pdev, bool enable)
{
    uint16_t command = pci_device_read_header(pdev, COMMAND);
    command = BIT_ENABLE(command, 0, enable);
    pci_device_write_header(pdev, COMMAND, command);
}

void pci_device_enable_memory(struct pci_device *pdev, bool enable)
{
    uint16_t command = pci_device_read_header(pdev, COMMAND);
    command = BIT_ENABLE(command, 1, enable);
    pci_device_write_header(pdev, COMMAND, command);
}

void pci_device_enable_bus_master(struct pci_device *pdev, bool enable)
{
    uint16_t command = pci_device_read_header(pdev, COMMAND);
    command = BIT_ENABLE(command, 2, enable);
    pci_device_write_header(pdev, COMMAND, command);
}

static void pci_device_enable_interrupts(struct pci_device *pdev, bool enable)
{
    uint16_t command = pci_device_read_header(pdev, COMMAND);
    command = BIT_ENABLE(command, 10, !enable); /* 0 = enabled */
    pci_device_write_header(pdev, COMMAND, command);
}

error_t pci_device_register(struct pci_device *pdev)
{
    return device_register(&pdev->device);
}

static error_t pci_bus_probe(struct pci_bus *bus);

static struct pci_bus *pci_bus_register_with_parent(struct pci_bus *parent)
{
    struct pci_bus *bus;
    uint8_t number = 0;

    bus = kmalloc(sizeof(struct pci_bus), KMALLOC_KERNEL);
    if (bus == NULL)
        return PTR_ERR(E_NOMEM);

    /* highest_bus_number + 1 */
    if (parent)
        number = to_pci_bus(llist_tail(pci_registered_buses))->number + 1;

    bus->number = number;
    bus->parent = parent;
    llist_add_tail(&pci_registered_buses, &bus->this);

    /* Recursively identify and register all children of this bus */
    pci_bus_probe(bus);

    return bus;
}

/** Dynamically register a new PCI-PCI bridge
 *  @note all connected devices are probed
 */
static struct pci_bus *pci_bridge_register(struct pci_device *device)
{
    struct pci_bus *parent = device->bus;
    struct pci_bus *bridge;
    struct pci_bus *subordinate;

    bridge = pci_bus_register_with_parent(parent);
    if (IS_ERR(bridge))
        return bridge;

    subordinate = to_pci_bus(llist_tail(&bridge->this));
    pci_device_write_header(device, BRIDGE,
                            PCI_HEADER_BRIDGE_SUBORDINATE(subordinate->number) |
                                PCI_HEADER_BRIDGE_SECONDARY(bridge->number) |
                                PCI_HEADER_BRIDGE_PRIMARY(parent->number));

    log_dbg("pci", "registered bridge %d [parent: %d, subordinate: %d]",
            bridge->number, parent->number, subordinate->number);

    return bridge;
}

static void pci_device_setup_bars(struct pci_device *device)
{
    uint32_t bar;
    uint32_t size;

    /* 6.2.5.1 */
    for (int i = 0; i < PCI_BAR_MAX_COUNT; ++i) {
        bar = pci_device_read_config(device, PCI_HEADER_BAR_OFFSET(i),
                                     PCI_HEADER_BAR_SIZE);
        if (!bar)
            continue;

        device->bars[i].type = bar & PCI_BAR_MEMORY_SPACE_MASK;

        pci_device_write_config(device, PCI_HEADER_BAR_OFFSET(i),
                                PCI_HEADER_BAR_SIZE, ALL_ONES);
        size = pci_device_read_config(device, PCI_HEADER_BAR_OFFSET(i),
                                      PCI_HEADER_BAR_SIZE);

        switch (device->bars[i].type) {
        case PCI_BAR_MEMORY:
            if ((bar & PCI_BAR_MEMORY_TYPE_MASK) != PCI_BAR_MEMORY_32B) {
                log_warn("pci", "%d.%d: unsupported memory bar[%d] type",
                         device->bus->number, device->number, i);
                continue;
            }
            size = ~(size & PCI_BAR_MEMORY_ADDRESS_MASK) + 1;
            device->bars[i].size = size;
            device->bars[i].phys = bar & PCI_BAR_MEMORY_ADDRESS_MASK;
            device->bars[i].data = kmalloc_dma_at(device->bars[i].phys, size);
            break;

        case PCI_BAR_IO:
            size = ~(size & PCI_BAR_IO_ADDRESS_MASK) + 1;
            device->bars[i].size = size;
            device->bars[i].phys = bar & PCI_BAR_IO_ADDRESS_MASK;
            break;
        }

        pci_device_write_config(device, PCI_HEADER_BAR_OFFSET(i),
                                PCI_HEADER_BAR_SIZE, bar);
    }
}

static error_t __pci_device_handle_interrupt(void *device)
{
    struct pci_device *pdev = device;
    error_t ret;

    if (!pdev->interrupt_handler)
        return E_SUCCESS;

    interrupts_disable();

    ret = pdev->interrupt_handler(pdev->interrupt_data);

    pic_eoi(pdev->interrupt_line);
    interrupts_enable();

    return ret;
}

error_t pci_device_register_interrupt_handler(struct pci_device *pdev,
                                              interrupt_handler handler,
                                              void *data)
{
    uint8_t interrupt;

    pdev->interrupt_line = pci_device_read_header(pdev, INTERRUPT_LINE);
    interrupt = PIC_MASTER_VECTOR + pdev->interrupt_line;

    if (!pdev->interrupt_line)
        return E_SUCCESS;

    /* TODO: Implement MSI (+ remove dependency on arch-specifi IRQ) */
    if (interrupts_has_been_installed(interrupt)) {
        log_warn("pci",
                 "another interrupt has already been installed on the "
                 "interrupt line (" LOG_FMT_8 ")",
                 interrupt);
        return E_BUSY;
    }

    pdev->interrupt_data = data;
    pdev->interrupt_handler = handler;

    interrupts_set_handler(interrupt, __pci_device_handle_interrupt, pdev);
    pic_enable_irq(pdev->interrupt_line);

    pci_device_enable_interrupts(pdev, true);

    return E_SUCCESS;
}

/** Configure and probe a present PCI device
 *
 *  If the device is a PCI-PCI bridge, the devices connected to this child bus
 *  are also recursively probed.
 */
static error_t pci_device_probe(struct pci_device *device, pci_header_type type)
{
    driver_t *driver = NULL;
    struct pci_bus *bus = device->bus;
    error_t ret = E_SUCCESS;

    device->id = pci_read_header_id(bus->number, device->number);

    pci_device_setup_bars(device);

    switch (type & PCI_HEADER_TYPE_MASK) {
    case PCI_HEADER_TYPE_PCI_BRIDGE:
        ret = ERR_FROM_PTR(pci_bridge_register(device));
        if (ret)
            return ret;
        break;

    case PCI_HEADER_TYPE_GENERAL:
        driver = driver_find_match(&device->device);
        if (driver != NULL)
            driver_probe(driver, &device->device);
        break;

    case PCI_HEADER_TYPE_CARDBUS_BRIDGE:
    case PCI_HEADER_TYPE_MULTI_FUNCTION:
        return E_NOT_SUPPORTED;
    }

    return E_SUCCESS;
}

/** Enumerate and probe all present PCI devices on a bus
 *
 * If PCI-PCI bridges are detected, they are automatically configured
 * and their connected devices are recursively probed.
 *
 * @see PCI spec chapter 6-1
 */
static error_t pci_bus_probe(struct pci_bus *bus)
{
    pci_header_type header_type;
    struct pci_device_id pci_id;
    struct pci_device *device;
    error_t ret = E_SUCCESS;

    for (int i = 0; i < PCI_MAX_DEVICE; ++i) {
        pci_id = pci_read_header_id(bus->number, i);
        if (pci_id.vendor == 0xFFFF)
            continue;

        header_type = pci_read_header(bus->number, i, TYPE);
        if (header_type & PCI_HEADER_TYPE_MULTI_FUNCTION)
            log_warn("pci", "Not implemented: multi function host controller");

        device = kcalloc(1, sizeof(*device), KMALLOC_KERNEL);
        if (device == NULL)
            return E_NOMEM;

        device->number = i;
        device->bus = bus;

        ret = pci_device_probe(device, header_type);
        if (ret)
            return ret;

        ret = pci_device_register(device);
        if (ret)
            return ret;
    }

    return E_SUCCESS;
}

static error_t pci_probe(device_t *device)
{
    struct pci_bus *root;

    UNUSED(device);

    root = pci_bus_register_with_parent(NULL);
    if (IS_ERR(root))
        return ERR_FROM_PTR(root);

    return E_SUCCESS;
}

static struct acpi_driver pci_driver = {
    .compatible = "PNP0A03",
    .driver =
        {
            .name = "pci",
            .operations =
                {
                    .probe = pci_probe,
                },
        },
};

ACPI_DECLARE_DRIVER(pci, &pci_driver);
