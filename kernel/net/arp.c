#define LOG_DOMAIN "arp"

#include <kernel/devices/ethernet.h>
#include <kernel/net/interface.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/net.h>
#include <kernel/init.h>
#include <kernel/net/arp.h>
#include <kernel/net/packet.h>
#include <kernel/spinlock.h>
#include <kernel/waitqueue.h>

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
	bool active;
	struct waitqueue waiters;
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
 * Allocate and initialize a new ARP table entry.
 */
static struct arp_entry *arp_entry_new(__be ipv4_t ip)
{
	struct arp_entry *entry;

	entry = kmalloc(sizeof(struct arp_header), KMALLOC_KERNEL);
	if (entry == NULL) {
		log_err("failed to allocate ARP table entry");
		return NULL;
	}

	entry->active = false;
	entry->prot_addr = ip;
	entry->hash.key = &entry->prot_addr;
	INIT_WAITQUEUE(entry->waiters);

	return entry;
}

/*
 * Free an ARP table entry.
 */
static void arp_entry_destroy(struct arp_entry *entry)
{
	locked_scope(&arp_table_lock) {
		entry->active = false;
		hashtable_remove(&arp_table, &entry->hash);
	}

	waitqueue_dequeue_all(&entry->waiters);

	kfree(entry);
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
 * Find the hardware address associated with a given IP from the local ARP table.
 */
const mac_address_t *arp_get(__be ipv4_t ip)
{
	const mac_address_t *mac = NULL;
	const struct arp_entry *entry;

	spinlock_acquire(&arp_table_lock);
	entry = arp_get_entry_locked(ip);
	if (entry == NULL)
		goto out;

	if (!entry->active)
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
		entry = arp_entry_new(ip);

		/*
		 * If the same entry was added since calling arp_get() use this
		 * one instead and free the one we just allocated. This is done
		 * to avoid calling kmalloc() while holding the spinlock.
		 */
		spinlock_acquire(&arp_table_lock);
		duplicate = arp_get_entry_locked(ip);
		if (!duplicate) {
			hashtable_insert(&arp_table, &entry->hash);
			log_dbg("%p4 -> %pm", &ip, mac);
		} else
			SWAP(entry, duplicate);
	}

	if (!entry->active) {
		entry->active = true;
		memcpy(entry->hw_addr, mac, sizeof(mac_address_t));
		waitqueue_dequeue_all(&entry->waiters);
	}

	spinlock_release(&arp_table_lock);

	if (duplicate)
		arp_entry_destroy(duplicate);

	return E_SUCCESS;
}

/*
 * Send an ARP packet.
 */
static error_t arp_send_packet(struct ethernet_device *netdev,
			       struct arp_header *arp,
			       mac_address_t dst_mac)
{
	struct packet *packet;

	packet = packet_new(ARP_PACKET_SIZE);
	if (IS_ERR(packet))
		return ERR_FROM_PTR(packet);

	packet->netdev = netdev;
	ethernet_fill_packet(packet, ETH_PROTO_ARP, dst_mac);
	packet_mark_l3_start(packet);
	packet_put(packet, arp, sizeof(*arp));

	return packet_send(packet);
}

/*
 * Broadcast an ARP request to all network interfaces.
 */
static void arp_broadcast_request(__be ipv4_t ip)
{
	struct net_interface *iface;
	mac_address_t bcast_mac;
	struct arp_header arp = {
		.hw_type = htons(ARP_HW_ETHERNET),
		.prot_type = htons(ETH_PROTO_IP),
		.hw_length = sizeof(mac_address_t),
		.prot_length = sizeof(ipv4_t),
		.operation = htons(ARP_REQUEST),
		.dst_ip = ip,
	};

	memset(bcast_mac, 0xff, sizeof(bcast_mac));

	/* TODO: lock registered_net_interfaces !!! */
	FOREACH_LLIST_ENTRY(iface, &registered_net_interfaces, this) {
		struct subnet *subnet;

		if (llist_is_empty(&iface->subnets))
		    continue;

		subnet = llist_first_entry(&iface->subnets, struct subnet, this);
		memcpy(arp.src_mac, iface->netdev->mac, sizeof(arp.src_mac));
		arp.src_ip = subnet->ip;
		arp_send_packet(iface->netdev, &arp, bcast_mac);
	}
}

/*
 * Find the hardware address associated with an IP.
 *
 * An ARP request is sent to the network if the address is not present
 * inside the local ARP table.
 */
const mac_address_t *arp_request(__be ipv4_t ip)
{
	mac_address_t *mac = NULL;
	struct arp_entry *entry;
	bool new_entry = false;

	/* necessary to allocate memory */
	ASSERT(interrupts_enabled());

	spinlock_acquire(&arp_table_lock);
	entry = arp_get_entry_locked(ip);
	if (!entry) {
		new_entry = true;
		entry = arp_entry_new(ip);
		if (!entry) {
			spinlock_release(&arp_table_lock);
			return NULL;
		}
		hashtable_insert(&arp_table, &entry->hash); /* insert inactive entry */
	}

	if (entry->active) {
		mac = &entry->hw_addr;
		spinlock_release(&arp_table_lock);
		return mac;
	}

	/* TODO: ARP retry + request timeout */
	spinlock_release(&arp_table_lock);
	waitqueue_lock(&entry->waiters);
	if (new_entry)
		arp_broadcast_request(ip);
	waitqueue_enqueue_locked(&entry->waiters, current);

	/* must re-fetch in case waiters were woken up by arp_destroy(). */
	entry = arp_get_entry(ip);
	if (!entry)
		return NULL;

	return &entry->hw_addr;
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
		log_dbg("Reply %pm has %p4", arp->src_mac, &arp->src_ip);
		return arp_add(arp->src_ip, arp->src_mac);

	case ARP_REQUEST:
		log_dbg("Request who has %p4? tell %p4 (%pm)",
			&arp->dst_ip,
			&arp->src_ip,
			arp->src_mac);
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

		return arp_send_packet(packet->netdev, &reply, reply.dst_mac);
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
