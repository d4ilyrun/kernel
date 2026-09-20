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
#include <kernel/spinlock.h>

#include <utils/bits.h>
#include <utils/macro.h>

#include <string.h>

#define RTL8139_TX_DESCRIPTOR_COUNT 4
#define RTL8139_TX_DESCRIPTOR_SIZE  2048
#define RTL8139_RX_BUFFER_SIZE	    8192
#define RTL8139_MTU		    1500

struct rtl8139_txq_desc {
	struct packet *packet;
	u16 status_reg;
	void *buffer;
};

struct rtl8139_txq {
	struct rtl8139_txq_desc descs[RTL8139_TX_DESCRIPTOR_COUNT];
	unsigned int desc_count;
	unsigned int desc_wr_index;
	unsigned int desc_rd_index;
	unsigned int desc_free_count;
	spinlock_t lock;
};

struct rtl8139 {
	void *registers;
	struct ethernet_device *netdev;
	struct pci_device *pci;
	struct rtl8139_txq txq;
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
	TSAD0 = 0x20,
	TSAD1 = 0x24,
	TSAD2 = 0x28,
	TSAD3 = 0x2C,
	RX_BUFFER_START = 0x30,
	COMMAND = 0x37,
	CURRENT_PACKET_READ = 0x38,
	INTERRUPT_MASK = 0x3C,	 /** Mask out interrupt sources */
	INTERRUPT_STATUS = 0x3E, /** Indicate the source of an interrupt */
	TRANSMIT_CFG = 0x40,
	RECEIVE_CFG = 0x44,
	CONFIG1 = 0x52,
};

#define RTL8139_CONFIG1_LWACT_OFFSET		4
#define RTL8139_CONFIG1_LWACT			BIT(RTL8139_CONFIG1_LWACT_OFFSET)
#define RTL8139_COMMAND_RX_ENABLE		BIT(2)
#define RTL8139_COMMAND_TX_ENABLE		BIT(3)
#define RTL8139_COMMAND_RESET			BIT(4)
#define RTL8139_TX_STATUS_OWN			BIT(13)
#define RTL8139_TX_STATUS_TOK			BIT(15)
#define RTL8139_RECEIVE_CFG_NO_WRAP		BIT(7)
#define RTL8139_RECEIVE_CFG_PHYSICAL		BIT(1)
#define RTL8139_RECEIVE_CFG_MULTICAST_OFFSET	2
#define RTL8139_RECEIVE_CFG_MULTICAST		BIT(RTL8139_RECEIVE_CFG_MULTICAST_OFFSET)
#define RTL8139_RECEIVE_CFG_BROADCAST_OFFSET	3
#define RTL8139_RECEIVE_CFG_BROADCAST		BIT(RTL8139_RECEIVE_CFG_BROADCAST_OFFSET)
#define RTL8139_RECEIVE_CFG_BUFFER_LENGTH(_len) (((((_len) / 8192) - 1) & 0x3) << 11)

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
#define RTL8139_PCI_BAR_IO     0
#define RTL8139_PCI_BAR_MEM    1

struct PACKED rtl8139_rx_packet {
	__be uint16_t status;
	__be uint16_t length;
	uint8_t packet[];
};

generate_device_rw_functions(rtl8139, struct rtl8139, registers, enum rtl8139_register);

/*
 * Reset NIC.
 */
static void rtl8139_reset(struct rtl8139 *rtl8139)
{
	rtl8139_writeb(rtl8139, COMMAND, RTL8139_COMMAND_RESET);
	WAIT_FOR(!(rtl8139_readb(rtl8139, COMMAND) & RTL8139_COMMAND_RESET));
}

/*
 *
 */
static void rtl8139_enable_transfer(struct rtl8139 *rtl8139, bool enable)
{
	uint8_t cmd = rtl8139_readb(rtl8139, COMMAND);

	if (enable)
		cmd |= RTL8139_COMMAND_RX_ENABLE | RTL8139_COMMAND_TX_ENABLE;
	else
		cmd &= ~(RTL8139_COMMAND_RX_ENABLE | RTL8139_COMMAND_TX_ENABLE);

	rtl8139_writeb(rtl8139, COMMAND, cmd);
}

/*
 * Get device mac address.
 */
static void rtl8139_get_mac(struct ethernet_device *device, mac_address_t mac)
{
	struct rtl8139 *rtl8139 = ethernet_device_priv(device);
	uint64_t mac_raw;

	mac_raw = be32toh(rtl8139_readl(rtl8139, ID0));
	mac_raw <<= 16;
	mac_raw |= be16toh(rtl8139_readw(rtl8139, ID4));

	ethernet_fill_mac(mac, mac_raw);
}

/*
 * Free all packets that have been sent to the wire, and update software TXQ.
 */
static void rtl8139_txq_reclaim(struct rtl8139 *rtl8139)
{
	struct rtl8139_txq *txq = &rtl8139->txq;
	unsigned int i;
	u32 val;

	i = txq->desc_rd_index;
	while (txq->desc_free_count != txq->desc_count) {
		struct rtl8139_txq_desc *txq_desc = &txq->descs[i];

		/* check if packet was sent successfully. */
		val = rtl8139_readl(rtl8139, txq_desc->status_reg);
		if (!(val & RTL8139_TX_STATUS_OWN) ||
		    !(val & RTL8139_TX_STATUS_TOK))
			break;

		packet_free(txq_desc->packet);
		txq_desc->packet = NULL;
		i = (i + 1) % txq->desc_count;
		txq->desc_free_count += 1;
	}
	txq->desc_rd_index = i;
}

/*
 *
 */
static error_t rtl8139_send_packet(struct ethernet_device *dev, struct packet *packet)
{
	struct rtl8139 *rtl8139 = ethernet_device_priv(dev);
	struct rtl8139_txq *txq = &rtl8139->txq;
	struct rtl8139_txq_desc *txq_desc;
	size_t data_size = packet_size(packet);

	if (data_size > dev->mtu)
		return E_INVAL;

	spinlock_acquire(&txq->lock);

	/* Try to free old descriptors. */
	rtl8139_txq_reclaim(rtl8139);
	if (txq->desc_free_count == 0) {
		/* TODO: backpressure */
		log_err("TX queue full");
		goto discard;
	}

	/* Setup DMA descriptor. */
	txq_desc = &rtl8139->txq.descs[txq->desc_wr_index];
	txq_desc->packet = packet;
	memcpy(txq_desc->buffer, packet_start(packet), data_size);
	rtl8139_writel(rtl8139, txq_desc->status_reg, data_size);

	/* update next descriptor index */
	txq->desc_wr_index = (txq->desc_wr_index + 1) % txq->desc_count;
	txq->desc_free_count -= 1;

	spinlock_release(&txq->lock);
	return E_SUCCESS;

discard:
	spinlock_release(&txq->lock);
	packet_free(packet);
	return E_INVAL;
}

/*
 *
 */
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
	rtl8139->rx_packet_offset = align_up(rtl8139->rx_packet_offset, sizeof(uint32_t));
	rtl8139->rx_packet_offset %= rtl8139->rx_buffer_size;

	/* See developper's guide, packet reception */
	rtl8139_writel(rtl8139, CURRENT_PACKET_READ, rtl8139->rx_packet_offset - 0x10);

	return ret;
}

static interrupt_return_t rtl8139_interrupt_handler(void *data)
{
	struct rtl8139 *rtl8139 = data;
	uint16_t isr = rtl8139_readw(rtl8139, INTERRUPT_STATUS);

	isr &= RTL8139_SUPPORTED_INTERRUPTS;
	if (isr == 0)
		return INTERRUPT_IGNORED; /* not for us. */
	rtl8139_writew(rtl8139, INTERRUPT_STATUS, isr);

	if (isr & INT_RX_OK)
		rtl8139_receive_packet(rtl8139);

	return INTERRUPT_HANDLED;
}

static struct ethernet_operations rtl8139_operations = {
    .send_packet = rtl8139_send_packet,
};

/*
 *
 */
static error_t rtl8139_init_txq(struct rtl8139 *rtl8139, struct rtl8139_txq *txq)
{
	memset(txq, 0, sizeof(*txq));
	txq->desc_count = RTL8139_TX_DESCRIPTOR_COUNT;
	txq->desc_free_count = txq->desc_count;

	for (unsigned int i = 0; i < txq->desc_count; ++i) {
		struct rtl8139_txq_desc *desc = &txq->descs[i];
		void *buffer;

		buffer = kmalloc_dma(RTL8139_TX_DESCRIPTOR_SIZE);
		if (buffer == NULL)
			return E_NOMEM;

		desc->buffer = buffer;
		desc->status_reg = TSD0 + i * sizeof(u32);
		rtl8139_writel(rtl8139, TSAD0 + i * sizeof(uint32_t),
			       mmu_find_physical((vaddr_t)buffer));
	}

	return E_SUCCESS;
}

/*
 *
 */
static void rtl8139_destroy(struct rtl8139 *rtl8139)
{
	/* Reset HW state. */
	rtl8139_reset(rtl8139);

	if (rtl8139->rx_buffer)
		kfree_dma(rtl8139->rx_buffer);

	/* Free TX queue. */
	for (unsigned int i = 0; i < rtl8139->txq.desc_count; ++i) {
		struct rtl8139_txq_desc *desc = &rtl8139->txq.descs[i];
		if (desc->packet) {
			packet_free(desc->packet);
			desc->packet = NULL;
		}
		if (desc->buffer)
			kfree_dma(desc->buffer);
	}

	ethernet_device_free(rtl8139->netdev);
}

/*
 *
 */
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
	rtl8139 = ethernet_device_priv(eth_dev);

	eth_dev->mtu = RTL8139_MTU;
	eth_dev->device = dev;
	eth_dev->ops = &rtl8139_operations;

	memset(rtl8139, 0, sizeof(*rtl8139));
	rtl8139->registers = pdev->bars[RTL8139_PCI_BAR_MEM].data;
	rtl8139->netdev = eth_dev;
	rtl8139->pci = pdev;

	pci_device_enable_memory(pdev, true);
	pci_device_enable_bus_master(pdev, true);

	/* set the LWAKE + LWPTN to active high, this should power on the device */
	rtl8139_writeb(rtl8139, CONFIG1, 0);
	rtl8139_reset(rtl8139);

	ret = rtl8139_init_txq(rtl8139, &rtl8139->txq);
	if (ret) {
		log_err("failed to init TX queue");
		goto probe_failed;
	}


	tx_cfg = rtl8139_readl(rtl8139, TRANSMIT_CFG);
	if (!rtl8139_is_rev_supported(tx_cfg & RTL8139_REV_MASK)) {
		log_err("invalid revision: %x", tx_cfg & RTL8139_REV_MASK);
		ethernet_device_free(eth_dev);
		return E_NOT_SUPPORTED;
	}

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
	rx_cfg = RTL8139_RECEIVE_CFG_MULTICAST | RTL8139_RECEIVE_CFG_BROADCAST |
		 RTL8139_RECEIVE_CFG_PHYSICAL | RTL8139_RECEIVE_CFG_NO_WRAP |
		 RTL8139_RECEIVE_CFG_BUFFER_LENGTH(RTL8139_RX_BUFFER_SIZE);

	rtl8139_writel(rtl8139, RECEIVE_CFG, rx_cfg);
	rtl8139_writel(rtl8139, RX_BUFFER_START, mmu_find_physical((vaddr_t)rx_buffer));

	ret = ethernet_device_register(eth_dev);
	if (ret)
		goto probe_failed;

	/* TODO: This should be done by userland via ioctl() or netlink for Linux compatibility. */
	net_interface_add_subnet(eth_dev->interface, IPV4(10, 1, 1, 2), 24);

	/* Configure and enable interrupts. */
	ret = pci_device_install_interrupt_handler(pdev, rtl8139_interrupt_handler, rtl8139);
	if (ret)
		goto probe_failed;
	rtl8139_writew(rtl8139, INTERRUPT_MASK, RTL8139_SUPPORTED_INTERRUPTS);

	rtl8139_enable_transfer(rtl8139, true);

	return E_SUCCESS;

probe_failed:
	rtl8139_destroy(rtl8139);
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

DECLARE_PCI_DRIVER(rtl8139, &rtl8139_driver);
