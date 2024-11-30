#include <kernel/devices/ethernet.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/net.h>
#include <kernel/net/arp.h>
#include <kernel/net/packet.h>

#include <libalgo/linked_list.h>
#include <utils/container_of.h>
#include <utils/macro.h>

#include <string.h>

struct arp_entry {
    LLIST_NODE(this);
    __be ipv4_t prot_addr;
    mac_address_t hw_addr;
};

/** The ARP table.
 *  It contains all known translations from IP address to MAC address.
 *
 *  TODO: Use a better datastructure than a list (hash map)
 *  TODO: Locking for multithreading r/w
 */
static DECLARE_LLIST(arp_table);

static int __arp_match_ip(const void *entry_ptr, const void *ip)
{
    const struct arp_entry *entry;
    entry = container_of(entry_ptr, struct arp_entry, this);
    return (entry->prot_addr == (__be ipv4_t)ip) ? COMPARE_EQ : !COMPARE_EQ;
}

static struct arp_entry *arp_get_entry(__be ipv4_t ip)
{
    node_t *entry_node = llist_find_first(arp_table, (void *)ip,
                                          __arp_match_ip);

    if (entry_node == NULL)
        return NULL;

    return container_of(entry_node, struct arp_entry, this);
}

const mac_address_t *arp_get(__be ipv4_t ip)
{
    const struct arp_entry *entry = arp_get_entry(ip);

    if (entry == NULL)
        return NULL;

    return &entry->hw_addr;
}

static error_t arp_add(__be ipv4_t ip, mac_address_t mac)
{
    struct arp_entry *entry = arp_get_entry(ip);

    /* If an entry for this IP is not already present, create and insert it */
    if (entry == NULL) {
        entry = kmalloc(sizeof(struct arp_header), KMALLOC_KERNEL);
        if (entry == NULL)
            return E_NOMEM;
        entry->prot_addr = ip;
        llist_add(&arp_table, &entry->this);
    }

    log_dbg("arp", LOG_FMT_IP " -> " LOG_FMT_MAC, LOG_IP(ip), LOG_MAC_ARG(mac));
    memcpy(entry->hw_addr, mac, sizeof(mac_address_t));

    return E_SUCCESS;
}

static error_t
arp_fill_packet(struct packet *packet, const struct arp_header *reply)
{
    ethernet_fill_packet(packet, ETH_PROTO_ARP, reply->dst_mac);
    packet_mark_l3_start(packet);
    return packet_put(packet, reply, sizeof(*reply));
}

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
    arp_fill_packet(packet, arp);

    return packet_send(packet);
}

error_t arp_receive_packet(struct packet *packet)
{
    struct arp_header *arp = packet->l3.arp;
    struct arp_header reply;
    const mac_address_t *reply_mac;

    if (ntoh(arp->hw_type) != ARP_HW_ETHERNET) {
        log_warn("arp", "Unsupported hardware type: " LOG_FMT_16,
                 ntoh(arp->hw_type));
        return E_NOT_SUPPORTED;
    }

    if (ntoh(arp->prot_type) != ETH_PROTO_IP) {
        log_warn("arp", "Unsupported protocol type: " LOG_FMT_16,
                 ntoh(arp->prot_type));
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
        reply.dst_ip = reply.src_ip;
        reply.src_ip = reply.dst_ip;
        memcpy(reply.dst_mac, arp->src_mac, sizeof(mac_address_t));
        memcpy(reply.src_mac, *reply_mac, sizeof(mac_address_t));

        return arp_send_packet(&reply);
    }

    log_warn("arp", "Received invalid ARP operation: " LOG_FMT_16,
             ntoh(arp->operation));

    return E_INVAL;
}
