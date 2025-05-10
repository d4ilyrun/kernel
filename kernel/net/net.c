#include <kernel/net.h>

/** Reference implementation of the Internet checksum algorithm */
u16 net_internet_checksum(const u8 *addr, size_t count)
{
    u32 sum = 0;

    while (count > 1) {
        sum += *addr++;
        count -= 2;
    }

    /*  Add left-over byte, if any */
    if (count > 0)
        sum += *(unsigned char *)addr;

    /*  Fold 32-bit sum to 16 bits */
    while (sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);

    return ~sum;
}
