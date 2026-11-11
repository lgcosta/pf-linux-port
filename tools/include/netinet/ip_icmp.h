/* PFLINUX: shadow <netinet/ip_icmp.h> — glibc's copy lacks the extended
 * ICMP type/code constants pfctl_parser.c's icmp-type keyword table
 * references.  Values copied verbatim from
 * _vendor/openbsd/obbsd/sys/netinet/ip_icmp.h (OpenBSD 8.0). */
#ifndef _PFLINUX_NETINET_IP_ICMP_H
#define _PFLINUX_NETINET_IP_ICMP_H

#include_next <netinet/ip_icmp.h>

#ifndef ICMP_ALTHOSTADDR
#define	ICMP_ALTHOSTADDR	6	/* alternate host address */
#endif
#ifndef ICMP_TRACEROUTE
#define	ICMP_TRACEROUTE		30	/* traceroute */
#endif
#ifndef ICMP_DATACONVERR
#define	ICMP_DATACONVERR	31	/* data conversion error */
#endif
#ifndef ICMP_MOBILE_REDIRECT
#define	ICMP_MOBILE_REDIRECT	32	/* mobile host redirect */
#endif
#ifndef ICMP_IPV6_WHEREAREYOU
#define	ICMP_IPV6_WHEREAREYOU	33	/* IPv6 where-are-you */
#endif
#ifndef ICMP_IPV6_IAMHERE
#define	ICMP_IPV6_IAMHERE	34	/* IPv6 i-am-here */
#endif
#ifndef ICMP_MOBILE_REGREQUEST
#define	ICMP_MOBILE_REGREQUEST	35	/* mobile registration req */
#endif
#ifndef ICMP_MOBILE_REGREPLY
#define	ICMP_MOBILE_REGREPLY	36	/* mobile registration reply */
#endif
#ifndef ICMP_SKIP
#define	ICMP_SKIP		39	/* SKIP */
#endif
#ifndef ICMP_PHOTURIS
#define	ICMP_PHOTURIS		40	/* Photuris */
#endif
#ifndef ICMP_PHOTURIS_UNKNOWN_INDEX
#define	ICMP_PHOTURIS_UNKNOWN_INDEX	1	/* unknown sec index */
#endif
#ifndef ICMP_PHOTURIS_AUTH_FAILED
#define	ICMP_PHOTURIS_AUTH_FAILED	2	/* auth failed */
#endif
#ifndef ICMP_PHOTURIS_DECRYPT_FAILED
#define	ICMP_PHOTURIS_DECRYPT_FAILED	3	/* decrypt failed */
#endif
#ifndef ICMP_PARAMPROB_ERRATPTR
#define	ICMP_PARAMPROB_ERRATPTR		0	/* req. opt. absent */
#endif
#ifndef ICMP_PARAMPROB_LENGTH
#define	ICMP_PARAMPROB_LENGTH		2	/* bad length */
#endif
#ifndef ICMP_ROUTERADVERT_NORMAL
#define	ICMP_ROUTERADVERT_NORMAL		0	/* normal advertisement */
#endif
#ifndef ICMP_ROUTERADVERT_NOROUTE_COMMON
#define	ICMP_ROUTERADVERT_NOROUTE_COMMON	16	/* selective routing */
#endif

#endif /* _PFLINUX_NETINET_IP_ICMP_H */
