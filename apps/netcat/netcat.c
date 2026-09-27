#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>

/*
 * Server: echo back everything the client sends.
 */
static void server_echo(int sock)
{
	char buffer[BUFSIZ];
	ssize_t bytes;

	while (true) {
		bytes = recv(sock, buffer, sizeof(buffer), 0);
		if (bytes == 0)
			break;
		if (bytes < 0) {
			switch (errno) {
			default:
				/* ignore error */
				continue;
			}
		}

		write(STDOUT_FILENO, buffer, bytes);
	}

out:
	return;
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
	close(sock);
}

/*
 *
 */
static void tcp_server(unsigned int port)
{
	struct sockaddr_in sin;
	int sock;
	int client_sock;
	int ret;

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port = htons(port);

	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
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

	ret = listen(sock, 16);
	if (ret < 0) {
		perror("listen");
		close(sock);
		return;
	}

	while (true) {
		client_sock = accept(sock, NULL, NULL);
		if (client_sock < 0) {
			perror("accept");
			continue;
		}

		/* Only accept one connection. */
		close(sock);
		break;
	}

	server_echo(client_sock);
	close(client_sock);
}

#define GETOP_OPTS "utl"

static void usage(const char *exe)
{
	printf("Usage: %s [-u|-t] [-l] port\n", exe);
	printf("Options:");
	printf("\t-l\tAct as a server\n");
	printf("\t-t\tUse TCP (default)\n");
	printf("\t-u\tUse UDP\n");
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

	if (is_server) {
		if (is_tcp) {
			tcp_server(port);
		} else {
			udp_server(port);
		}
	} else {
		puts("Client not supported yet");
		usage(argv[0]);
		exit(1);
	}

	return EXIT_SUCCESS;
}
