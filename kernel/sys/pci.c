#include <kernel/cpu.h>
#include <kernel/pci.h>

#include <utils/bits.h>
#include <utils/math.h>

#define PCI_CFG_DATA (0xCFC)
#define PCI_CFG_ADDRESS (0xCF8)
#define PCI_CFG_ADDRESS_ENABLE BIT(31)
#define PCI_CFG_ADDRESS_BUS(_bus) ((uint32_t)(_bus) << 16)
#define PCI_CFG_ADDRESS_DEVICE(_dev) ((uint32_t)(_dev) << 11)
#define PCI_CFG_ADDRESS_FUNCTION(_func) ((uint32_t)(_func) << 8)
#define PCI_CFG_ADDRESS_OFFSET(_off) ((uint32_t)(_off))

uint32_t
pci_read_config(uint8_t bus, uint8_t device, uint8_t offset, size_t size)
{
    uint32_t size_mask = BIT64(size * 8) - 1;
    unsigned int reg_offset = offset % sizeof(uint32_t);
    uint32_t cfg_address;
    uint32_t cfg_data;

    cfg_address = PCI_CFG_ADDRESS_ENABLE | PCI_CFG_ADDRESS_BUS(bus) |
                  PCI_CFG_ADDRESS_DEVICE(device) |
                  PCI_CFG_ADDRESS_OFFSET(align_down(offset, sizeof(uint32_t)));

    outl(PCI_CFG_ADDRESS, cfg_address);
    cfg_data = inl(PCI_CFG_DATA);

    cfg_data = le32toh(cfg_data);
    cfg_data >>= reg_offset * 8;

    return cfg_data & size_mask;
}

void pci_write_config(uint8_t bus, uint8_t device, uint8_t offset, size_t size,
                      uint32_t value)
{
    uint32_t size_mask = BIT64(size * 8) - 1;
    unsigned int reg_offset = offset % sizeof(uint32_t);
    uint32_t cfg_address;
    uint32_t cfg_data;

    cfg_address = PCI_CFG_ADDRESS_ENABLE | PCI_CFG_ADDRESS_BUS(bus) |
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
