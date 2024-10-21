/**
 * @file kernel/devices/pci.h
 *
 * @defgroup kernel_device_pci PCI
 * @ingroup kernel_device
 *
 * @see https://wiki.osdev.org/PCI
 * @see docs/specs/pci_local_bus.pdf - Chapter 6: Configuration Space
 *
 * @{
 */

#ifndef KERNEL_DEVICES_PCI_H
#define KERNEL_DEVICES_PCI_H

#include <kernel/devices/driver.h>

#include <utils/container_of.h>

// WARNING: This struct is only valid in case of little endian alignment
struct PACKED pci_device_id {
    uint16_t vendor;
    uint16_t device;
};

#define PCI_DEVICE_ID(_vendor, _device) \
    ((struct pci_device_id){_vendor, _device})

/** Per-bus driver struct for PCI drivers
 *  @see device_driver
 */
struct pci_driver {
    struct device_driver driver;
    struct pci_device_id compatible; ///< The
};

/** A PCI bus */
struct pci_bus {
    node_t this;            ///< Intrusive node list used to enumerate PCI buses
    uint8_t number;         ///< The bus's number
    struct pci_bus *parent; ///< The parent bus (NULL if this is the root bus)
};

/** Per-bus device struct for PCI devices
 *  @see device
 */
struct pci_device {

    struct device device;

    uint8_t number;          ///< The device number on its bus
    struct pci_bus *bus;     ///< The bus to which the device is connected
    struct pci_device_id id; ///< The PCI device's vendor/device ID

#define PCI_MAX_BAR_COUNT 6

    struct {
        u64 value;
        enum {
            PCI_BAR_MEMORY,
            PCI_BAR_IO
        } type;
    } bars[PCI_MAX_BAR_COUNT];
};

#define to_pci_drv(_this) container_of(_this, struct pci_driver, driver)
#define to_pci_bus(_this) container_of(_this, struct pci_bus, this)
#define to_pci_dev(_this) container_of(_this, struct pci_device, device)

void pci_driver_register(struct pci_driver *);

#define PCI_DECLARE_DRIVER(_name, _driver) \
    DECLARE_DRIVER(_name, _driver, pci_driver_register)

/** Register a PCI device */
error_t pci_device_register(struct pci_device *);

#endif /* KERNEL_DEVICES_PCI_H */

/** @} */
