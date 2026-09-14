#include <arpa/inet.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/in.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#define exit(...) return __VA_ARGS__

static int open_udp_socket(int addr, int port)
{
        struct sockaddr_in sin;
        int ret;
        int fd;

        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = addr;
        sin.sin_port = port;

        fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) {
                printf("socket(): %s\n", strerror(errno));
		return -1;
        }

	ret = bind(fd, (void *)&sin, sizeof(sin));
	if (ret < 0) {
                printf("connect(): %s\n", strerror(errno));
		close(fd);
		return -1;
	}

        return fd;
}

int main(void)
{
	struct sockaddr_in sin;
	int fd;

	sin.sin_family = AF_INET;
	sin.sin_port = 3232;
        fd = open_udp_socket(0x0201010A, ntohs(3232));
	if (fd < 0)
		goto out;

	// while (1) {
	// 	write(fd, "toto", 4);
	// }

	while (1) {
		char buf[256];
		ssize_t len;

		len = read(fd, buf, sizeof(buf));
		if (len < 0) {
			printf("read(): %s\n", strerror(errno));
		} else {
			printf("got: %s (%ld)\n", buf, len);
		}
	}

out:
	while (1) {}
        return EXIT_SUCCESS;
}
