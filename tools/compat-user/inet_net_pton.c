/*
 * PFLINUX: BSD inet_net_pton(3) for pfctl on glibc.  glibc 2.43 declares
 * the function in <arpa/inet.h> but no longer provides the symbol (the
 * resolver dropped it); libc.so.6 exports neither.  pfctl_parser.c uses
 * it only for AF_INET abbreviated-CIDR parsing ("10/8", "10.0.0.0/255...",
 * "10.1.2" classful).  Semantics implemented per OpenBSD
 * lib/libc/net/inet_net_pton.c:
 *   - returns prefix length on success, -1 with errno on failure.
 *   - writes the network address (masked) to pbin.
 *   - AF_INET6 accepts CIDR via inet_pton (RFC 5952 form), default /128.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
netmask_bits(uint32_t mask)
{
	int bits = 0;

	while ((mask & 0x80000000u) != 0) {
		bits++;
		mask <<= 1;
	}
	return ((mask == 0) ? bits : -1);	/* must be contiguous */
}

static int
pton4(const char *pres, struct in_addr *pin, size_t binsize)
{
	char copy[128], *sp;
	struct in_addr base;
	uint32_t hostaddr, mask;
	int bits = -1;

	if (strlen(pres) >= sizeof(copy)) {
		errno = EMSGSIZE;
		return (-1);
	}
	strcpy(copy, pres);

	sp = strchr(copy, '/');
	if (sp == NULL) {
		if (!inet_aton(copy, &base))
			return (-1);
		hostaddr = ntohl(base.s_addr);
		/* classful defaults, matching BSD */
		if (hostaddr < 0x80000000u)
			bits = 8;
		else if (hostaddr < 0xC0000000u)
			bits = 16;
		else
			bits = 24;
	} else {
		*sp++ = '\0';
		if (!inet_aton(copy, &base))
			return (-1);

		if (strchr(sp, '.')) {
			struct in_addr m;
			if (!inet_aton(sp, &m))
				return (-1);
			bits = netmask_bits(ntohl(m.s_addr));
			if (bits < 0)
				return (-1);
		} else {
			char *end;
			long v;

			if (*sp == '\0')
				return (-1);
			v = strtol(sp, &end, 10);
			if (end == sp || *end != '\0' || v < 0 || v > 32)
				return (-1);
			bits = (int)v;
		}
	}

	hostaddr = ntohl(base.s_addr);
	mask = bits == 0 ? 0 : (0xffffffffu << (32 - bits));
	hostaddr &= mask;

	if (binsize < sizeof(base)) {
		errno = EMSGSIZE;
		return (-1);
	}
	pin->s_addr = htonl(hostaddr);
	return (bits);
}

static int
pton6(const char *pres, void *pbin, size_t binsize)
{
	char copy[128], *sp;
	int bits = 128;

	if (strlen(pres) >= sizeof(copy)) {
		errno = EMSGSIZE;
		return (-1);
	}
	strcpy(copy, pres);
	sp = strchr(copy, '/');
	if (sp != NULL) {
		*sp++ = '\0';
		if (*sp == '\0')
			return (-1);
		bits = (int)strtol(sp, NULL, 10);
		if (bits < 0 || bits > 128)
			return (-1);
	}

	/* glibc inet_pton accepts bare addresses; mask by prefix by hand */
	if (binsize < sizeof(struct in6_addr)) {
		errno = EMSGSIZE;
		return (-1);
	}
	if (inet_pton(AF_INET6, copy, pbin) != 1)
		return (-1);
	if (bits < 128) {
		uint8_t *a = pbin;
		int i;

		for (i = bits / 8; i < 16; i++)
			a[i] = 0;
		a[bits / 8] &= (uint8_t)(0xffu << (8 - (bits % 8)));
	}
	return (bits);
}

int
inet_net_pton(int af, const char *pres, void *pbin, size_t binsize)
{
	if (pres == NULL || pbin == NULL) {
		errno = EFAULT;
		return (-1);
	}
	switch (af) {
	case AF_INET:
		return (pton4(pres, pbin, binsize));
	case AF_INET6:
		return (pton6(pres, pbin, binsize));
	default:
		errno = EAFNOSUPPORT;
		return (-1);
	}
}
