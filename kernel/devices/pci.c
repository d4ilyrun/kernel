#include <kernel/devices/acpi.h>
#include <kernel/devices/pci.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>

#include <utils/bits.h>
#include <utils/container_of.h>
#include <utils/macro.h>

#define PCI_MAX_BUS 256
#define PCI_MAX_DEVICE 32

#define PCI_CFG_DATA (0xCFC)
#define PCI_CFG_ADDRESS (0xCF8)
#define PCI_CFG_ADDRESS_ENABLE BIT(31)
#define PCI_CFG_ADDRESS_BUS(_bus) ((uint32_t)(_bus) << 16)
#define PCI_CFG_ADDRESS_DEVICE(_dev) ((uint32_t)(_dev) << 11)
#define PCI_CFG_ADDRESS_FUNCTION(_func) ((uint32_t)(_func) << 8)
#define PCI_CFG_ADDRESS_OFFSET(_off) ((uint32_t)(_off))

#define PCI_HEADER_ID_OFFSET 0x0
#define PCI_HEADER_ID_SIZE sizeof(uint32_t)
#define PCI_HEADER_TYPE_OFFSET 0xE
#define PCI_HEADER_TYPE_SIZE sizeof(uint8_t)
#define PCI_HEADER_BAR_OFFSET(_bar) (0x10 + (_bar)*PCI_HEADER_BAR_SIZE)
#define PCI_HEADER_BAR_SIZE sizeof(uint32_t)

#define PCI_HEADER_BRIDGE_OFFSET 0x18
#define PCI_HEADER_BRIDGE_SIZE (3 * sizeof(uint8_t))
#define PCI_HEADER_BRIDGE_SUBORDINATE(_n) ((_n) << 16)
#define PCI_HEADER_BRIDGE_SECONDARY(_n) ((_n) << 8)
#define PCI_HEADER_BRIDGE_PRIMARY(_n) (_n)

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

#define PCI_HEADER_TYPE_MASK 0x7F

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

static inline uint32_t pci_device_read_register(struct pci_bus *bus,
                                                uint8_t device, uint8_t offset,
                                                size_t size)
{
    uint32_t size_mask = BIT64(size * 8) - 1;
    unsigned int reg_offset = offset % sizeof(uint32_t);
    uint32_t cfg_address;
    uint32_t cfg_data;

    cfg_address = PCI_CFG_ADDRESS_ENABLE | PCI_CFG_ADDRESS_BUS(bus->number) |
                  PCI_CFG_ADDRESS_DEVICE(device) |
                  PCI_CFG_ADDRESS_OFFSET(align_down(offset, sizeof(uint32_t)));

    outl(PCI_CFG_ADDRESS, cfg_address);
    cfg_data = inl(PCI_CFG_DATA);

    cfg_data = le32toh(cfg_data);
    cfg_data >>= reg_offset * 8;

    return cfg_data & size_mask;
}

static inline void pci_device_write_register(struct pci_bus *bus,
                                             uint8_t device, uint8_t offset,
                                             size_t size, uint32_t value)
{
    uint32_t size_mask = BIT64(size * 8) - 1;
    unsigned int reg_offset = offset % sizeof(uint32_t);
    uint32_t cfg_address;
    uint32_t cfg_data;

    cfg_address = PCI_CFG_ADDRESS_ENABLE | PCI_CFG_ADDRESS_BUS(bus->number) |
                  PCI_CFG_ADDRESS_DEVICE(device) |
                  PCI_CFG_ADDRESS_OFFSET(align_down(offset, sizeof(uint32_t)));

    outl(PCI_CFG_ADDRESS, cfg_address);
    cfg_data = inl(PCI_CFG_DATA);

    /* Clear old value */
    cfg_data = le32toh(cfg_data);
    cfg_data &= ~(size_mask << (reg_offset * 8));

    /* Replace with new value */
    value &= size_mask;
    cfg_data |= value << (reg_offset * 8);

    outl(PCI_CFG_DATA, cfg_data);
}

static inline pci_header_type pci_device_read_header_type(struct pci_bus *bus,
                                                          uint8_t device)
{
    return pci_device_read_register(bus, device, PCI_HEADER_TYPE_OFFSET,
                                    PCI_HEADER_TYPE_SIZE);
}

static inline struct pci_device_id pci_device_read_id(struct pci_bus *bus,
                                                      uint8_t device)
{
    uint32_t id = pci_device_read_register(bus, device, PCI_HEADER_ID_OFFSET,
                                           PCI_HEADER_ID_SIZE);
    return *(struct pci_device_id *)&id;
}

error_t pci_device_register(struct pci_device *device)
{
    uint32_t bar;

    for (int i = 0; i < PCI_MAX_BAR_COUNT; ++i) {
        bar = pci_device_read_register(device->bus, device->number,
                                       PCI_HEADER_BAR_OFFSET(i),
                                       PCI_HEADER_BAR_SIZE);
        log_info("pci", "BAR%d @ %02X = " LOG_FMT_32, i,
                 PCI_HEADER_BAR_OFFSET(i), bar);
    }

    return E_SUCCESS;
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
static struct pci_bus *pci_bridge_register(struct pci_bus *parent,
                                           struct pci_device *device)
{
    struct pci_bus *bridge;
    struct pci_bus *subordinate;

    bridge = pci_bus_register_with_parent(parent);
    if (IS_ERR(bridge))
        return bridge;

    if (parent) {
        subordinate = to_pci_bus(llist_tail(&bridge->this));
        pci_device_write_register(
            parent, device->number, PCI_HEADER_BRIDGE_OFFSET,
            PCI_HEADER_BRIDGE_SIZE,
            PCI_HEADER_BRIDGE_SUBORDINATE(subordinate->number) |
                PCI_HEADER_BRIDGE_SECONDARY(bridge->number) |
                PCI_HEADER_BRIDGE_PRIMARY(parent->number));
        log_dbg("pci", "registered bridge %d [parent: %d, subordinate: %d]",
                bridge->number, parent->number, subordinate->number);
    }

    return bridge;
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

    switch (type & PCI_HEADER_TYPE_MASK) {
    case PCI_HEADER_TYPE_PCI_BRIDGE:
        ret = ERR_FROM_PTR(pci_bridge_register(bus, device));
        if (ret)
            return ret;
        break;

    case PCI_HEADER_TYPE_GENERAL:
        device->id = pci_device_read_id(bus, device->number);
        log_info("pci", "device %02x:%02x is a device with id V%04X:D%04X",
                 bus->number, device->number, device->id.vendor,
                 device->id.device);
        driver = driver_find_match(&device->device);
        if (driver != NULL) {
            log_info("pci", "Found driver for device %02x:%02x: '%s'",
                     bus->number, device->number, driver->name);
            driver_probe(driver, &device->device);
        }
        break;

    case PCI_HEADER_TYPE_CARDBUS_BRIDGE:
    case PCI_HEADER_TYPE_MULTI_FUNCTION:
        break;
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
        pci_id = pci_device_read_id(bus, i);
        if (pci_id.vendor == 0xFFFF)
            continue;

        header_type = pci_device_read_header_type(bus, i);
        if (header_type & PCI_HEADER_TYPE_MULTI_FUNCTION)
            log_warn("pci", "Not implemented: multi function host controller");

        device = kcalloc(1, sizeof(*device), KMALLOC_KERNEL);
        if (device == NULL)
            return E_NOMEM;

        device->number = i;
        ret = pci_device_probe(device, header_type);
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
