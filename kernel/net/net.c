#include <kernel/net.h>

/*
 * Reference internet checksum computed over multiple distinct buffers.
 */
u16 net_internet_checksum_vec(const struct iovec *iov, size_t iov_len)
{
	u32 sum = 0;
	bool have_odd = false;
	u16 odd;

	for (size_t i = 0; i < iov_len; ++i) {
		size_t size = iov[i].iov_len;
		const u16 *addr = iov[i].iov_base;

		if (size <= 0)
			continue;

		if (have_odd) {
			sum += (odd << 8) | *(u8 *)addr;
			addr = (void *)((u8 *)addr + 1);
			size -= sizeof(u8);
			have_odd = false;
		}

		while (size > sizeof(u8)) {
			sum += ntoh(*addr++);
			size -= sizeof(u16);
		}

		if (size) {
			have_odd = true;
			odd = *(u8 *)addr;
		}
	}

	if (have_odd > 0)
		sum += odd << 8;

	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);

	return ntohs(~sum);
}

/*
 * Reference internet checksum (RFC 1071).
 */
u16 net_internet_checksum(const u16 *addr, size_t size)
{
	struct iovec iov = {
		.iov_base = (void *)addr,
		.iov_len = size,
	};

	return net_internet_checksum_vec(&iov, 1);
}
