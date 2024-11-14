#pragma once

#include <kernel/types.h>

#include <utils/macro.h>

#define PCI_HEADER_ID_OFFSET 0x0
#define PCI_HEADER_ID_SIZE sizeof(uint32_t)
#define PCI_HEADER_TYPE_OFFSET 0xE
#define PCI_HEADER_TYPE_SIZE sizeof(uint8_t)
#define PCI_HEADER_BAR_OFFSET(_bar) (0x10 + (_bar)*PCI_HEADER_BAR_SIZE)
#define PCI_HEADER_BAR_SIZE sizeof(uint32_t)

#define PCI_HEADER_COMMAND_OFFSET 0x4
#define PCI_HEADER_COMMAND_SIZE sizeof(uint16_t)

#define PCI_HEADER_BRIDGE_OFFSET 0x18
#define PCI_HEADER_BRIDGE_SIZE (3 * sizeof(uint8_t))
#define PCI_HEADER_BRIDGE_SUBORDINATE(_n) ((_n) << 16)
#define PCI_HEADER_BRIDGE_SECONDARY(_n) ((_n) << 8)
#define PCI_HEADER_BRIDGE_PRIMARY(_n) (_n)

#define PCI_HEADER_INTERRUPT_LINE_OFFSET 0x3C
#define PCI_HEADER_INTERRUPT_LINE_SIZE sizeof(uint8_t)

/** Read data from the PCI configuration space */
uint32_t
pci_read_config(uint8_t bus, uint8_t device, uint8_t offset, size_t size);

/** Write data into the PCI configuration space */
void pci_write_config(uint8_t bus, uint8_t device, uint8_t offset, size_t size,
                      uint32_t data);

#define pci_read_header(_bus, _device, _header)                            \
    pci_read_config(_bus, _device, CONCAT3(PCI_HEADER_, _header, _OFFSET), \
                    CONCAT3(PCI_HEADER_, _header, _SIZE))

#define pci_write_header(_bus, _device, _header, _val)                      \
    pci_write_config(_bus, _device, CONCAT3(PCI_HEADER_, _header, _OFFSET), \
                     CONCAT3(PCI_HEADER_, _header, _SIZE), _val)
