/* PFLINUX: shadow <netinet/ip.h> — glibc's copy lacks the IPTOS_DSCP_CS*
 * class-selector codepoints parse.y's `set tos` table references (plus a
 * few neighbours); values copied verbatim from
 * _vendor/openbsd/obbsd/sys/netinet/ip.h (OpenBSD 8.0). */
#ifndef _PFLINUX_NETINET_IP_H
#define _PFLINUX_NETINET_IP_H

#include_next <netinet/ip.h>

#ifndef IPTOS_DSCP_CS0
#define	IPTOS_DSCP_CS0		0x00
#endif
#ifndef IPTOS_DSCP_LE
#define	IPTOS_DSCP_LE		0x04
#endif
#ifndef IPTOS_DSCP_CS1
#define	IPTOS_DSCP_CS1		0x20
#endif
#ifndef IPTOS_DSCP_CS2
#define	IPTOS_DSCP_CS2		0x40
#endif
#ifndef IPTOS_DSCP_CS3
#define	IPTOS_DSCP_CS3		0x60
#endif
#ifndef IPTOS_DSCP_CS4
#define	IPTOS_DSCP_CS4		0x80
#endif
#ifndef IPTOS_DSCP_CS5
#define	IPTOS_DSCP_CS5		0xa0
#endif
#ifndef IPTOS_DSCP_VA
#define	IPTOS_DSCP_VA		0xb0
#endif
#ifndef IPTOS_DSCP_EF
#define	IPTOS_DSCP_EF		0xb8
#endif
#ifndef IPTOS_DSCP_CS6
#define	IPTOS_DSCP_CS6		0xc0
#endif
#ifndef IPTOS_DSCP_CS7
#define	IPTOS_DSCP_CS7		0xe0
#endif

#endif /* _PFLINUX_NETINET_IP_H */
