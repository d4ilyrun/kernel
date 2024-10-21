#include <kernel/devices/pci.h>
#include <kernel/logger.h>

#include <utils/macro.h>

static error_t rtl8139_probe(struct device *dev)
{
    UNUSED(dev);
    log_info("rtl8139", "probing");
    return pci_device_register(to_pci_dev(dev));
}

struct pci_driver rtl8139_driver = {
    .compatible = PCI_DEVICE_ID(0x10EC, 0x8139),
    .driver =
        {
            .name = "rtl8139",
            .operations.probe = rtl8139_probe,
        },
};

PCI_DECLARE_DRIVER(rtl8139, &rtl8139_driver);
