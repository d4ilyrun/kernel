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
#include <kernel/interrupts.h>
#include <kernel/pci.h>

#include <utils/container_of.h>

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
    struct pci_device_id compatible;
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

    u8 number;               ///< The device number on its bus
    struct pci_bus *bus;     ///< The bus to which the device is connected
    struct pci_device_id id; ///< The PCI device's vendor/device ID

    u8 interrupt_line; ///< The PIC interrupt number used by the PCI device
    interrupt_handler interrupt_handler; ///< The interrupt handler routine
    void *interrupt_data; /// Data passed to the interrupt routine

#define PCI_BAR_MAX_COUNT 6

    /** PCI Base Address Registers */
    struct pci_bar {
        void *data;   ///< Addressable virtual address mapped to the register
        paddr_t phys; ///< The BAR's "true" physical or IO address
        size_t size;  ///< The address register's size
        /** The type of the Address Register */
        enum pci_bar_type {
            PCI_BAR_MEMORY, ///< Physical memory (either 32 or 64b)
            PCI_BAR_IO      ///< IO memory
        } type;
    } bars[PCI_BAR_MAX_COUNT];
};

#define to_pci_drv(_this) container_of(_this, struct pci_driver, driver)
#define to_pci_bus(_this) container_of(_this, struct pci_bus, this)
#define to_pci_dev(_this) container_of(_this, struct pci_device, device)

void pci_driver_register(struct pci_driver *);

#define DECLARE_PCI_DRIVER(_name, _driver) \
    DECLARE_DRIVER(_name, _driver, pci_driver_register)

/** Register a PCI device */
error_t pci_device_register(struct pci_device *);

/** Register a custom interrupt handler function for this device
 *
 *  @param interrupt_handler The interrupt handler function
 *  @param data The data passed to the interrupt handler
 */
error_t pci_device_register_interrupt_handler(struct pci_device *,
                                              interrupt_handler, void *data);

/** Enable/Disable a device's response to I/O space accesses */
void pci_device_enable_io(struct pci_device *, bool);

/** Enable/Disable a device's response to memory space accesses */
void pci_device_enable_memory(struct pci_device *, bool);

/** Enable/Disable a device's ability to perform bus-master operations */
void pci_device_enable_bus_master(struct pci_device *, bool);

static inline void pci_device_write_config(struct pci_device *pdev,
                                           uint8_t offset, size_t size,
                                           uint32_t value)
{
    pci_write_config(pdev->bus->number, pdev->number, offset, size, value);
}

static inline uint32_t
pci_device_read_config(struct pci_device *dev, uint8_t offset, size_t size)
{
    return pci_read_config(dev->bus->number, dev->number, offset, size);
}

#endif /* KERNEL_DEVICES_PCI_H */

/** @} */
