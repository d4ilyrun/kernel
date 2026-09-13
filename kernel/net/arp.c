#define LOG_DOMAIN "arp"

#include <kernel/devices/ethernet.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/net.h>
#include <kernel/init.h>
#include <kernel/net/arp.h>
#include <kernel/net/packet.h>
#include <kernel/spinlock.h>

#include <libalgo/hashtable.h>
#include <utils/container_of.h>
#include <utils/macro.h>

#include <string.h>

/*
 * Entry inside the ARP table.
 *
 * TODO: Entry timeout
 * TODO: Reference counting ?
 */
struct arp_entry {
	__be ipv4_t prot_addr;
	mac_address_t hw_addr;
	struct hashtable_entry hash;
};

/** The ARP table.
 *  It contains all known translations from IP address to MAC address.
 *
 *  TODO: Use RW lock
 */
static DECLARE_HASHTABLE(arp_table, 256);
static DECLARE_SPINLOCK(arp_table_lock);

/*
 * Hashing function used by the ARP hash table.
 */
static u32 arp_hash(const void *key)
{
	return hash32(*(u32 *)key);
}

/*
 * Comparison function used by the ARP hash table.
 */
static int arp_hash_compare(const void *left_key, const void *right_key)
{
	if (*(u32 *)left_key != *(u32 *)right_key)
		return !COMPARE_EQ;

	return COMPARE_EQ;
}

/*
 * Find an entry inside the ARP table while holding @arp_table_lock.
 */
static struct arp_entry *arp_get_entry_locked(__be ipv4_t ip)
{
	const struct hashtable_entry *entry;

	entry  = hashtable_find(&arp_table, &ip);
	if (!entry)
		return NULL;

	return container_of(entry, struct arp_entry, hash);
}

/*
 * Find an entry inside the ARP table.
 */
static struct arp_entry *arp_get_entry(__be ipv4_t ip)
{
	locked_scope(&arp_table_lock) {
		return arp_get_entry_locked(ip);
	}

	assert_not_reached();
}

/*
 * Find the hardware address associated with a given IP.
 */
const mac_address_t *arp_get(__be ipv4_t ip)
{
	const mac_address_t *mac = NULL;
	const struct arp_entry *entry;

	spinlock_acquire(&arp_table_lock);
	entry = arp_get_entry_locked(ip);
	if (entry == NULL)
		goto out;

	mac = &entry->hw_addr;
out:
	spinlock_release(&arp_table_lock);
	return mac;
}

/*
 * Add an entry inside the ARP table.
 */
error_t arp_add(__be ipv4_t ip, mac_address_t mac)
{
	struct arp_entry *duplicate = NULL;
	struct arp_entry *entry;

	spinlock_acquire(&arp_table_lock);
	entry = arp_get_entry_locked(ip);
	if (entry == NULL) {

		spinlock_release(&arp_table_lock);
		entry = kmalloc(sizeof(struct arp_header), KMALLOC_KERNEL);
		if (entry == NULL)
			return E_NOMEM;

		/*
		 * If the same entry was added since calling arp_get() use this
		 * one instead and free the one we just allocated. This is done
		 * to avoid calling kmalloc() while holding the spinlock.
		 */
		spinlock_acquire(&arp_table_lock);
		duplicate = arp_get_entry_locked(ip);
		if (!duplicate) {
			entry->prot_addr = ip;
			entry->hash.key = &ip;
			hashtable_insert(&arp_table, &entry->hash);
			log_dbg(FMT_IP " -> " FMT_MAC, LOG_IP(ip), LOG_MAC_ARG(mac));
		} else
			SWAP(entry, duplicate);
	}

	memcpy(entry->hw_addr, mac, sizeof(mac_address_t));
	spinlock_release(&arp_table_lock);

	if (duplicate)
		kfree(duplicate);

	return E_SUCCESS;
}

/*
 * Send an ARP packet.
 */
error_t arp_send_packet(struct arp_header *arp)
{
	struct packet *packet = packet_new(ARP_PACKET_SIZE);
	struct ethernet_device *netdev;

	if (IS_ERR(packet))
		return ERR_FROM_PTR(packet);

	netdev = ethernet_device_find_by_mac(arp->src_mac);
	if (IS_ERR(netdev))
		return ERR_FROM_PTR(netdev);
	packet->netdev = netdev;

	ethernet_fill_packet(packet, ETH_PROTO_ARP, arp->dst_mac);
	packet_mark_l3_start(packet);
	packet_put(packet, arp, sizeof(*arp));

	return packet_send(packet);
}

/*
 * Handle received ARP packet received by the Ethernet layer.
 */
error_t arp_receive_packet(struct packet *packet)
{
	struct arp_header *arp = packet->l3.arp;
	struct arp_header reply;
	const mac_address_t *reply_mac;
	ipv4_t tmp;

	if (ntoh(arp->hw_type) != ARP_HW_ETHERNET) {
		log_warn("Unsupported hardware type: " FMT16, ntoh(arp->hw_type));
		return E_NOT_SUPPORTED;
	}

	if (ntoh(arp->prot_type) != ETH_PROTO_IP) {
		log_warn("Unsupported protocol type: " FMT16, ntoh(arp->prot_type));
		return E_NOT_SUPPORTED;
	}

	switch (ntoh(arp->operation)) {
	case ARP_REPLY:
		return arp_add(arp->dst_ip, arp->dst_mac);

	case ARP_REQUEST:
		arp_add(arp->src_ip, arp->src_mac);

		reply_mac = arp_get(arp->dst_ip);
		if (reply_mac == NULL)
			return E_NOENT;

		/* Copy header, switch src/dst and insert found hw address into src */
		memcpy(&reply, arp, arp_header_size(arp));
		reply.operation = htons(ARP_REPLY);
		tmp = reply.dst_ip;
		reply.dst_ip = reply.src_ip;
		reply.src_ip = tmp;
		memcpy(reply.dst_mac, arp->src_mac, sizeof(mac_address_t));
		memcpy(reply.src_mac, *reply_mac, sizeof(mac_address_t));

		return arp_send_packet(&reply);
	}

	log_warn("Received invalid ARP operation: " FMT16, ntoh(arp->operation));

	return E_INVAL;
}

/*
 * Initialize the ARP table.
 */
error_t arp_init(void)
{
	hashtable_init(&arp_table, arp_hash, arp_hash_compare);

	return E_SUCCESS;
}

DECLARE_INITCALL(INIT_EARLY, arp_init);
