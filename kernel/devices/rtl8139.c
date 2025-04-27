#define LOG_DOMAIN "rtl8139"

#include <kernel/devices/ethernet.h>
#include <kernel/devices/pci.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/mmu.h>
#include <kernel/net.h>
#include <kernel/net/ethernet.h>
#include <kernel/net/interface.h>
#include <kernel/net/ipv4.h>
#include <kernel/net/packet.h>

#include <utils/bits.h>
#include <utils/macro.h>

#include <string.h>

#define RTL8139_TX_DESCRIPTOR_COUNT 4
#define RTL8139_TX_DESCRIPTOR_SIZE 2048
#define RTL8139_RX_BUFFER_SIZE 8192
#define RTL8139_MTU 1500

struct rtl8139 {
    void *registers;
    struct ethernet_device *netdev;
    struct pci_device *pci;
    void *tx_descriptors[RTL8139_TX_DESCRIPTOR_COUNT];
    unsigned int tx_current_decriptor : 2;
    void *rx_buffer;
    size_t rx_buffer_size;
    uint16_t rx_packet_offset;
};

/* 5.7 - Hardware revision ID */
enum rtl8139_revision {
    RTL8139 = 0x60000000,
    RTL8139A = 0x70000000,
    RTL8139AG = 0x74000000,
    RTL8139B = 0x78000000,
    RTL8130 = RTL8139B,
    RTL8139C = RTL8139AG,
    RTL8100 = 0x78800000,
    RTL8139D = 0x74400000,
    RTL8100B = RTL8139D,
    RTL8139CPLUS = 0x74800000,
    RTL8101 = 0x74C00000,
};

static inline bool rtl8139_is_rev_supported(enum rtl8139_revision rev)
{
    switch (rev) {
    case RTL8139:
    case RTL8139A:
    case RTL8139AG:
    case RTL8139B:
    case RTL8139CPLUS:
        return true;
    case RTL8100:
    case RTL8139D:
    case RTL8101:
        return false;
    }

    return false;
}

#define RTL8139_REV_MASK 0x7CC00000

enum rtl8139_register {
    ID0 = 0x00, /** MAC address, higher 4 bytes */
    ID4 = 0x04, /** MAC address, lower 2 bytes */
    TSD0 = 0x10,
    TSD1 = 0x14,
    TSD2 = 0x18,
    TSD3 = 0x1C,
    TX_STATUS_DESCRIPTOR = TSD0, /** TX buffers status */
    TSAD0 = 0x20,
    TSAD1 = 0x24,
    TSAD2 = 0x28,
    TSAD3 = 0x2C,
    TX_START_ADDRESS = TSAD0, /** Physical start address of TX buffers */
    RX_BUFFER_START = 0x30,
    COMMAND = 0x37,
    CURRENT_PACKET_READ = 0x38,
    INTERRUPT_MASK = 0x3C,   /** Mask out interrupt sources */
    INTERRUPT_STATUS = 0x3E, /** Indicate the source of an interrupt */
    TRANSMIT_CFG = 0x40,
    RECEIVE_CFG = 0x44,
    CONFIG1 = 0x52,
};

#define RTL8139_CONFIG1_LWACT_OFFSET 4
#define RTL8139_CONFIG1_LWACT BIT(RTL8139_CONFIG1_LWACT_OFFSET)
#define RTL8139_COMMAND_RX_ENABLE BIT(2)
#define RTL8139_COMMAND_TX_ENABLE BIT(3)
#define RTL8139_COMMAND_RESET BIT(4)
#define RTL8139_TX_STATUS_OWN BIT(13)
#define RTL8139_RECEIVE_CFG_NO_WRAP BIT(7)
#define RTL8139_RECEIVE_CFG_PHYSICAL BIT(1)
#define RTL8139_RECEIVE_CFG_MULTICAST_OFFSET 2
#define RTL8139_RECEIVE_CFG_MULTICAST BIT(RTL8139_RECEIVE_CFG_MULTICAST_OFFSET)
#define RTL8139_RECEIVE_CFG_BROADCAST_OFFSET 3
#define RTL8139_RECEIVE_CFG_BROADCAST BIT(RTL8139_RECEIVE_CFG_BROADCAST_OFFSET)
#define RTL8139_RECEIVE_CFG_BUFFER_LENGTH(_len) \
    (((((_len) / 8192) - 1) & 0x3) << 11)

enum rtl8139_interrupt_source {
    INT_RX_OK = BIT(0),
    INT_RX_ERR = BIT(1),
    INT_TX_OK = BIT(2),
    INT_TX_ERR = BIT(3),
    INT_RX_OVERFLOW = BIT(4),
    INT_TIMEOUT = BIT(14),
    INT_SYSTEM_ERR = BIT(15),
};

#define RTL8139_SUPPORTED_INTERRUPTS (INT_RX_OK | INT_TX_OK)

#define RTL8139_REGISTERS_SIZE 256
#define RTL8139_PCI_BAR_IO 0
#define RTL8139_PCI_BAR_MEM 1

struct PACKED rtl8139_rx_packet {
    __be uint16_t status;
    __be uint16_t length;
    uint8_t packet[];
};

generate_device_rw_functions(rtl8139, struct rtl8139, registers,
                             enum rtl8139_register);

static void rtl8139_soft_reset(struct rtl8139 *rtl8139)
{
    rtl8139_writeb(rtl8139, COMMAND, RTL8139_COMMAND_RESET);
    WAIT_FOR(!(rtl8139_readb(rtl8139, COMMAND) & RTL8139_COMMAND_RESET));

    rtl8139->tx_current_decriptor = 0;
    rtl8139->rx_packet_offset = 0;

    kfree_dma(rtl8139->rx_buffer);
    rtl8139->rx_buffer = NULL;
}

static void rtl8139_enable_transfer(struct rtl8139 *rtl8139, bool enable)
{
    uint8_t cmd = rtl8139_readb(rtl8139, COMMAND);

    if (enable)
        cmd |= RTL8139_COMMAND_RX_ENABLE | RTL8139_COMMAND_TX_ENABLE;
    else
        cmd &= ~(RTL8139_COMMAND_RX_ENABLE | RTL8139_COMMAND_TX_ENABLE);

    /*
     * NB: After initial power-up, software must insure that the transmitter has
     *     completely reset before setting this bit.
     */

    rtl8139_writeb(rtl8139, COMMAND, cmd);
}

static void rtl8139_get_mac(struct ethernet_device *device, mac_address_t mac)
{
    struct rtl8139 *rtl8139 = ethernet_device_priv(device);
    uint64_t mac_raw;

    mac_raw = be32toh(rtl8139_readl(rtl8139, ID0));
    mac_raw <<= 16;
    mac_raw |= be16toh(rtl8139_readw(rtl8139, ID4));

    ethernet_fill_mac(mac, mac_raw);
}

static error_t rtl8139_send_packet_to_descriptor(struct rtl8139 *rtl8139,
                                                 unsigned int descriptor,
                                                 struct packet *packet)
{
    if (descriptor > RTL8139_TX_DESCRIPTOR_COUNT)
        return E_INVAL;

    if (packet_size(packet) > RTL8139_TX_DESCRIPTOR_SIZE)
        return E_INVAL;

    memcpy(rtl8139->tx_descriptors[descriptor], packet_start(packet),
           packet_size(packet));

    rtl8139_writel(rtl8139,
                   TX_STATUS_DESCRIPTOR + (descriptor * sizeof(uint32_t)),
                   packet_size(packet));

    return E_SUCCESS;
}

static error_t
rtl8139_send_packet(struct ethernet_device *dev, struct packet *packet)
{
    /* TODO: Backpressure */
    struct rtl8139 *rtl8139 = ethernet_device_priv(dev);
    unsigned int descriptor = rtl8139->tx_current_decriptor++;
    return rtl8139_send_packet_to_descriptor(rtl8139, descriptor, packet);
}

static error_t rtl8139_receive_packet(struct rtl8139 *rtl8139)
{
    struct rtl8139_rx_packet *rx_packet;
    error_t ret = E_SUCCESS;
    struct packet *packet;
    size_t packet_length;

    rx_packet = rtl8139->rx_buffer + rtl8139->rx_packet_offset;
    packet_length = rx_packet->length - sizeof(uint32_t); /* remove CRC */

    /* 2. Copy packet from buffer and handle its content
     *
     * - We explicitely use NO wrapping, so no need to check for out of bounds
     */
    packet = packet_new(packet_length);
    if (IS_ERR(packet)) {
        log_warn("failed to copy received packet's content");
        ret = ERR_FROM_PTR(packet);
    } else {
        packet->netdev = rtl8139->netdev;
        packet_put(packet, rx_packet->packet, packet_length);
        ethernet_device_receive_packet(rtl8139->netdev, packet);
    }

    /* 4. Tell the NIC where to read the next packet
     *
     * - All packets are aligned on a DWORD boundary
     * - Wrap packet if necessary
     */
    rtl8139->rx_packet_offset += sizeof(struct rtl8139_rx_packet);
    rtl8139->rx_packet_offset += rx_packet->length;
    rtl8139->rx_packet_offset = align_up(rtl8139->rx_packet_offset,
                                         sizeof(uint32_t));
    rtl8139->rx_packet_offset %= rtl8139->rx_buffer_size;

    /* See developper's guide, packet reception */
    rtl8139_writel(rtl8139, CURRENT_PACKET_READ,
                   rtl8139->rx_packet_offset - 0x10);

    return ret;
}

static u32 rtl8139_interrupt_handler(void *data)
{
    struct rtl8139 *rtl8139 = data;
    uint16_t isr = rtl8139_readw(rtl8139, INTERRUPT_STATUS);

    if (isr & INT_RX_OK) {
        rtl8139_receive_packet(rtl8139);
    }

    /* clear interrupt source bit by writing one to the ISR (6.7) */
    rtl8139_writew(rtl8139, INTERRUPT_STATUS, isr);

    return E_SUCCESS;
}

static error_t rtl8139_enable_capability(struct ethernet_device *device,
                                         enum ethernet_capability cap,
                                         bool enable)
{
    struct rtl8139 *rtl8139 = ethernet_device_priv(device);
    uint32_t cfg;

    cfg = rtl8139_readl(rtl8139, RECEIVE_CFG);

    switch (cap) {
    case ETHERNET_CAP_BROADCAST:
        cfg = BIT_ENABLE(cfg, RTL8139_RECEIVE_CFG_BROADCAST_OFFSET, enable);
        break;
    case ETHERNET_CAP_MULTICAST:
        cfg = BIT_ENABLE(cfg, RTL8139_RECEIVE_CFG_MULTICAST_OFFSET, enable);
        break;

    default:
        return E_NOT_SUPPORTED;
    }

    device->capabilities = BIT_ENABLE(device->capabilities, cap, enable);
    rtl8139_writel(rtl8139, RECEIVE_CFG, cfg);

    return E_SUCCESS;
}

static struct ethernet_operations rtl8139_operations = {
    .send_packet = rtl8139_send_packet,
    .enable_capability = rtl8139_enable_capability,
};

static error_t rtl8139_probe(struct device *dev)
{
    struct pci_device *pdev = to_pci_dev(dev);
    struct ethernet_device *eth_dev;
    struct rtl8139 *rtl8139;
    void *rx_buffer;
    uint32_t tx_cfg;
    uint32_t rx_cfg;
    error_t ret;

    if (pdev->bars[RTL8139_PCI_BAR_MEM].size != RTL8139_REGISTERS_SIZE) {
        log_err("invalid register size: %ld", pdev->bars[0].size);
        return E_INVAL;
    }

    if (pdev->bars[RTL8139_PCI_BAR_MEM].type != PCI_BAR_MEMORY) {
        log_err("invalid register type");
        return E_INVAL;
    }

    eth_dev = ethernet_device_alloc(sizeof(*rtl8139));
    if (IS_ERR(eth_dev))
        return ERR_FROM_PTR(eth_dev);

    eth_dev->mtu = RTL8139_MTU;

    rtl8139 = ethernet_device_priv(eth_dev);
    if (rtl8139 == NULL)
        return E_NOMEM;

    memset(rtl8139, 0, sizeof(*rtl8139));
    rtl8139->registers = pdev->bars[RTL8139_PCI_BAR_MEM].data;
    rtl8139->netdev = eth_dev;
    rtl8139->pci = pdev;

    pci_device_enable_memory(pdev, true);
    pci_device_enable_bus_master(pdev, true);

    /* set the LWAKE + LWPTN to active high, this should power on the device */
    rtl8139_writeb(rtl8139, CONFIG1, 0);

    /* soft reset */
    rtl8139_soft_reset(rtl8139);

    tx_cfg = rtl8139_readl(rtl8139, TRANSMIT_CFG);
    if (!rtl8139_is_rev_supported(tx_cfg & RTL8139_REV_MASK)) {
        log_err("invalid revision: %x", tx_cfg & RTL8139_REV_MASK);
        ethernet_device_free(eth_dev);
        return E_NOT_SUPPORTED;
    }

    eth_dev->ops = &rtl8139_operations;
    rtl8139_get_mac(eth_dev, eth_dev->mac);

    /** Configure RX: (@see 6.9)
     *  - buffer size = 8K
     *  - accept multicast + broadcast + mac_address
     *  - no wrap when
     */
    rx_buffer = kmalloc_dma(RTL8139_RX_BUFFER_SIZE + RTL8139_MTU);
    if (rx_buffer == NULL) {
        log_err("Failed to allocate RX FIFO");
        ethernet_device_free(eth_dev);
        return E_NOMEM;
    }

    rtl8139->rx_buffer = rx_buffer;
    rtl8139->rx_buffer_size = RTL8139_RX_BUFFER_SIZE;
    eth_dev->capabilities = ETHERNET_CAP_BROADCAST | ETHERNET_CAP_MULTICAST;
    rx_cfg = RTL8139_RECEIVE_CFG_MULTICAST | RTL8139_RECEIVE_CFG_BROADCAST |
             RTL8139_RECEIVE_CFG_PHYSICAL | RTL8139_RECEIVE_CFG_NO_WRAP |
             RTL8139_RECEIVE_CFG_BUFFER_LENGTH(RTL8139_RX_BUFFER_SIZE);

    rtl8139_writel(rtl8139, RECEIVE_CFG, rx_cfg);
    rtl8139_writel(rtl8139, RX_BUFFER_START,
                   mmu_find_physical((vaddr_t)rx_buffer));

    /* Configure TX */
    ret = E_NOMEM;
    for (int i = 0; i < RTL8139_TX_DESCRIPTOR_COUNT; ++i) {
        void *tx = kmalloc_dma(RTL8139_TX_DESCRIPTOR_SIZE);
        if (tx == NULL)
            goto probe_failed;
        rtl8139->tx_descriptors[i] = tx;
        rtl8139_writel(rtl8139, TX_START_ADDRESS + i * sizeof(uint32_t),
                       mmu_find_physical((vaddr_t)tx));
    }

    ret = pci_device_register_interrupt_handler(pdev, rtl8139_interrupt_handler,
                                                rtl8139);
    if (ret)
        goto probe_failed;

    ret = ethernet_device_register(eth_dev);
    if (ret)
        goto probe_failed;

    net_interface_add_subnet(eth_dev->interface, IPV4(10, 1, 1, 2), 24);

    /* enable interrupts */
    rtl8139_writew(rtl8139, INTERRUPT_MASK, RTL8139_SUPPORTED_INTERRUPTS);
    rtl8139_enable_transfer(rtl8139, true);

    return E_SUCCESS;

probe_failed:
    if (rtl8139->rx_buffer)
        kfree_dma(rtl8139->rx_buffer);
    ethernet_device_free(eth_dev);
    return ret;
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
