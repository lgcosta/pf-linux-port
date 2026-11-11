/* PFLINUX: <net/route.h> — the userland-visible subset of OpenBSD's
 * <net/route.h>/<sys/socket.h> routing constants that net/pfvar.h
 * (RTLABEL_LEN) and parse.y (CTL_NET route lookups) rely on.  Linux'
 * glibc <net/route.h> carries only struct rtentry/ioctls, so this shim
 * shadows it.  Values copied from OpenBSD 8.0 sys/sys/socket.h and
 * sys/net/route.h; RT_TABLEID_MAX matches module/include/pfk.h (32),
 * the routing-domain ceiling our pf.ko implements. */
#ifndef _PFLINUX_NET_ROUTE_H
#define _PFLINUX_NET_ROUTE_H

#include <sys/types.h>

/* PFLINUX: AF_ROUTE/PF_ROUTE (== 17) come from glibc <sys/socket.h>
 * unmodified; only the routing-table constants pfvar.h/parse.y need
 * live here. */

#define NET_RT_DUMP	1
#define NET_RT_FLAGS	2
#define NET_RT_IFLIST	3
#define NET_RT_TABLE	5

#define RTLABEL_LEN	32
#define RT_TABLEID_MAX	32	/* keep in sync with module/include/pfk.h */

struct rt_tableinfo {
	u_short	rti_tableid;	/* routing table id */
	u_short	rti_domainid;	/* routing domain id */
};

struct rtentry;			/* only referenced via pointers/prototypes */

#endif /* _PFLINUX_NET_ROUTE_H */
