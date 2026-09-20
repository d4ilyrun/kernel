#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>

/*
 * Server: echo back everything the client sends.
 */
static void server_echo(int sock)
{
	char buffer[BUFSIZ];
	ssize_t bytes;

	while (true) {
		bytes = read(sock, buffer, sizeof(buffer));
		if (bytes < 0)
			continue;
		write(STDOUT_FILENO, buffer, bytes);
	}
}

/*
 *
 */
static void udp_server(unsigned int port)
{
	struct sockaddr_in sin;
	int sock;
	int ret;

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port = htons(port);

	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		perror("socket()");
		return;
	}

	ret = bind(sock, (void *)&sin, sizeof(sin));
	if (ret < 0) {
		perror("bind()");
		close(sock);
		return;
	}

	server_echo(sock);
}

#define GETOP_OPTS "ul"

static void usage(const char *exe)
{
	printf("Usage: %s [-u] [-l] port", exe);
}

int main(int argc, char *argv[])
{
	bool is_server = false;
	bool is_tcp = true;
	unsigned int port;
	const char *port_str;
	char *end;
	int opt;

	while ((opt = getopt(argc, argv, GETOP_OPTS)) != -1) {
		switch (opt) {
		case 'u':
			is_tcp = false;
			break;
		case 'l':
			is_server = true;
			break;

		default:
		case 'h':
			usage(argv[0]);
			exit(1);
		}
	}

	/* no port argument */
	if (argc <= optind) {
		usage(argv[0]);
		exit(1);
	}

	argc -= optind;
	argv += optind;

	port_str = argv[0];
	port = strtoul(port_str, &end, 10);
	if (port < INET_MIN_PORT || port > INET_MAX_PORT || *end != '\0') {
		printf("invalid port: %s\n", port_str);
		exit(1);
	}

	if (is_tcp) {
		puts("TCP not supported yet");
		usage(argv[0]);
		exit(1);
	}

	if (!is_server) {
		puts("Client not supported yet");
		usage(argv[0]);
		exit(1);
	}

	udp_server(port);

	return EXIT_SUCCESS;
}
