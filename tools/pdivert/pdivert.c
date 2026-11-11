/*
 * pdivert — PF-on-Linux divert(4) relay (ISC 2026)
 *
 * Mirrors the classic BSD divert(8) workflow against the in-module
 * PF_DIVERT socket family:
 *
 *	pdivert -p <port> [-f] [-q]
 *	  -p port   bind the divert socket to <port> (matches pf rule port)
 *	  -f        flip the last ICMP payload byte (demo rewrite)
 *	  -q        quiet (no hexdump)
 *
 * Packets pulled from the socket are sent straight back (optionally
 * mutated), reproducing "receive -> rewrite -> reinject".
 *
 * The kernel side (module/pfsrc/divert_sock.c) re-injects per OpenBSD
 * ip_divert.c semantics using the metadata captured at divert time.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifndef PF_DIVERT
#define PF_DIVERT	32	/* BSD PF_DIVERT; in-module family */
#endif
#ifndef IPPROTO_DIVERT
#define IPPROTO_DIVERT	254
#endif

int
main(int argc, char **argv)
{
	int fd, ch, port = 0, flip = 0, quiet = 0;
	unsigned char buf[65536];

	while ((ch = getopt(argc, argv, "p:fq")) != -1) {
		switch (ch) {
		case 'p':
			port = atoi(optarg);
			break;
		case 'f':
			flip = 1;
			break;
		case 'q':
			quiet = 1;
			break;
		default:
			fprintf(stderr, "usage: pdivert -p port [-fq]\n");
			return 2;
		}
	}
	if (port <= 0 || port > 65535) {
		fprintf(stderr, "pdivert: -p <port> required\n");
		return 2;
	}

	if ((fd = socket(PF_DIVERT, SOCK_RAW, IPPROTO_DIVERT)) == -1) {
		perror("socket(PF_DIVERT)");
		return 1;
	}
	{
		struct sockaddr_in sa;

		memset(&sa, 0, sizeof(sa));
		sa.sin_family = AF_INET;	/* family ignored by bind; port only */
		sa.sin_port = htons((uint16_t)port);
		if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
			perror("bind");
			return 1;
		}
	}
	if (!quiet)
		fprintf(stderr, "pdivert: listening on divert port %d\n", port);

	for (;;) {
		ssize_t n = recv(fd, buf, sizeof(buf), 0);

		if (n <= 0) {
			if (n < 0 && errno != EAGAIN && errno != EINTR)
				perror("recv");
			if (n == 0)
				break;
			continue;
		}
		if (!quiet) {
			char src[64] = "?", dst[64] = "?";

			if (n >= 20 && (buf[0] >> 4) == 4) {
				inet_ntop(AF_INET, buf + 12, src, sizeof(src));
				inet_ntop(AF_INET, buf + 16, dst, sizeof(dst));
			}
			fprintf(stderr, "pdivert: got %zd bytes %s -> %s\n",
			    n, src, dst);
		}
		if (flip && n > 0)
			buf[n - 1] ^= 0xff;
		if (send(fd, buf, (size_t)n, 0) == -1)
			perror("send");
	}
	close(fd);
	return 0;
}
