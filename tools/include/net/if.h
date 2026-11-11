/* PFLINUX: <net/if.h> — glibc's <net/if.h> plus OpenBSD's interface-group
 * ABI (sys/sockio.h) used by pfctl_parser.c (ifa_exists/ifa_grouplookup).
 * Linux has no interface groups: SIOCGIFGMEMB keeps the OpenBSD encoding
 * (it is computed with our OpenBSD _IOWR) and every use of it on an AF_INET
 * socket returns ENOTTY, so group lookups degrade to "not a group" exactly
 * like on an OpenBSD without the group. */
#ifndef _PFLINUX_NET_IF_H
#define _PFLINUX_NET_IF_H

#include_next <net/if.h>

#include <sys/ioctl.h>
#include <sys/types.h>

#ifndef IFNAMSIZ
#define IFNAMSIZ IF_NAMESIZE
#endif

#define IFG_ALL		"all"	/* group contains all interfaces */
#define IFG_EGRESS	"egress"	/* if(s) default route(s) point to */

struct ifg_req {
	union {
		char	ifgrqu_group[IFNAMSIZ];
		char	ifgrqu_member[IFNAMSIZ];
	} ifgrq_ifgrqu;
#define ifgrq_group	ifgrq_ifgrqu.ifgrqu_group
#define ifgrq_member	ifgrq_ifgrqu.ifgrqu_member
};

struct ifgroupreq {
	char	ifgr_name[IFNAMSIZ];
	u_int	ifgr_len;
	union {
		char		 ifgru_group[IFNAMSIZ];
		struct ifg_req	*ifgru_groups;
	} ifgr_ifgru;
#define ifgr_group	ifgr_ifgru.ifgru_group
#define ifgr_groups	ifgr_ifgru.ifgru_groups
};

#ifndef SIOCGIFGMEMB
#define SIOCGIFGMEMB	_IOWR('i', 138, struct ifgroupreq) /* OpenBSD value */
#endif

/* PFLINUX: OpenBSD if.h queue-priority range (parse.y `prio` check) */
#ifndef IFQ_NQUEUES
#define IFQ_NQUEUES	8
#endif
#ifndef IFQ_MAXPRIO
#define IFQ_MAXPRIO	(IFQ_NQUEUES - 1)
#endif

#endif /* _PFLINUX_NET_IF_H */
