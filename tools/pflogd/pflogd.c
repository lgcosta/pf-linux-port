/*
 * pflogd for PF-on-Linux (ISC 2026)
 *
 * Reads records from /dev/pflog (see module/pfsrc/pfk_pflog.c) and writes a
 * pcap file with linktype DLT_PFLOG (117) - the same format OpenBSD's pflogd
 * produces, so tcpdump -r / wireshark decode it.
 *
 *   pflogd [-i iface] [-f /dev/pflog] [-p pidfile] [-c count] <file>
 *
 * A "-" as <file> writes to stdout.  Omitting <file> prints a human-readable
 * one-line summary per record (useful in the lab gates).
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <netinet/in.h>

#define PFLOG_IFNAMSIZ		16
#define PFLOG_RULESET_NAME	16
#define DLT_PFLOG		117

struct pflog_hdr {
	int64_t		ts_sec;
	int64_t		ts_usec;
	uint8_t		af;
	uint8_t		reason;
	uint8_t		ifname[PFLOG_IFNAMSIZ];
	uint8_t		ruleset[PFLOG_RULESET_NAME];
	uint8_t		nr;
	uint8_t		anc;
	uint8_t		flags;
	uint8_t		pad[9];
	uint16_t	pktlen;		/* network order, as in BSD */
} __attribute__((packed));

struct pcap_file_header {
	uint32_t magic;
	uint16_t major, minor;
	int32_t thiszone;
	uint32_t sigfigs, snaplen, network;
};

struct pcap_pkthdr {
	uint32_t ts_sec, ts_usec, caplen, len;
};

static volatile int running = 1;
static void onterm(int s) { (void)s; running = 0; }

static const char *reason_name(uint8_t r)
{
	static const char *n[] = { "pass", "drop", "scrub", "noscrub",
	    "nat", "nonat", "rdr", "nordr" };
	return r < 8 ? n[r] : "?";
}

int
main(int argc, char **argv)
{
	const char *dev = "/dev/pflog", *file = NULL, *pidfile = NULL;
	long want = -1;
	int fd, out = 1, ch, wrote_header = 0, nrec = 0;
	unsigned char buf[sizeof(struct pflog_hdr) + 2048];

	while ((ch = getopt(argc, argv, "f:c:p:")) != -1)
		switch (ch) {
		case 'f': dev = optarg; break;
		case 'c': want = atol(optarg); break;
		case 'p': pidfile = optarg; break;
		default: goto usage;
		}
	if (optind < argc)
		file = argv[optind];

	signal(SIGINT, onterm);
	signal(SIGTERM, onterm);

	fd = open(dev, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "pflogd: open %s: %s\n", dev, strerror(errno));
		return 1;
	}
	if (file && strcmp(file, "-") != 0) {
		out = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (out < 0) {
			fprintf(stderr, "pflogd: open %s: %s\n", file,
			    strerror(errno));
			return 1;
		}
	}
	if (pidfile) {
		FILE *p = fopen(pidfile, "w");
		if (p) { fprintf(p, "%d\n", (int)getpid()); fclose(p); }
	}

	while (running) {
		ssize_t n, off = 0;
		struct pflog_hdr *h;

		/* read one full record: the chardev returns header+payload
		 * consecutively; a short read just continues where we left */
		while (off < (ssize_t)sizeof(*h)) {
			n = read(fd, buf + off, sizeof(buf) - off);
			if (n <= 0) {
				if (n < 0 && errno == EINTR)
					goto cont;
				if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
					fd_set r;
					struct timeval tv = { 0, 500000 };
					FD_ZERO(&r); FD_SET(fd, &r);
					if (select(fd + 1, &r, NULL, NULL, &tv) <= 0)
						goto cont;
					continue;
				}
				goto cont;
			}
			off += n;
		}
		h = (struct pflog_hdr *)buf;
		{
			uint16_t plen = ntohs(h->pktlen);
			while (off < (ssize_t)(sizeof(*h) + plen)) {
				n = read(fd, buf + off, sizeof(buf) - off);
				if (n <= 0)
					break;
				off += n;
			}
			if (off < (ssize_t)(sizeof(*h) + plen))
				goto cont;

			if (file) {
				struct pcap_file_header g = {
					.magic = 0xa1b2c3d4, .major = 2,
					.minor = 4, .thiszone = 0,
					.sigfigs = 0, .snaplen = 65535,
					.network = DLT_PFLOG,
				};
				struct pcap_pkthdr ph = {
					.ts_sec = (uint32_t)h->ts_sec,
					.ts_usec = (uint32_t)h->ts_usec,
					.caplen = sizeof(*h) + plen,
					.len = sizeof(*h) + plen,
				};
				if (!wrote_header) {
					if (write(out, &g, sizeof(g)) != sizeof(g))
						break;
					wrote_header = 1;
				}
				if (write(out, &ph, sizeof(ph)) != sizeof(ph) ||
				    write(out, buf, ph.caplen) != (ssize_t)ph.caplen)
					break;
			} else {
				printf("%lld.%06lld %s if=%s ruleset=%s nr=%u "
				    "len=%u\n", (long long)h->ts_sec,
				    (long long)h->ts_usec, reason_name(h->reason),
				    (char *)h->ifname, (char *)h->ruleset,
				    h->nr, plen);
				fflush(stdout);
			}
			nrec++;
			if (want > 0 && nrec >= want)
				break;
		}
cont:
		if (want > 0 && nrec >= want)
			break;
	}
	if (file && strcmp(file, "-") != 0)
		close(out);
	close(fd);
	fprintf(stderr, "pflogd: %d records\n", nrec);
	return 0;
usage:
	fprintf(stderr, "usage: pflogd [-f dev] [-c count] [-p pidfile] [file]\n");
	return 2;
}
