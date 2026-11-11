/* PFLINUX compat: <net/rtable.h> — minimal view of OpenBSD's routing
 * table API as used by PF. Linux has a single (policy-)routing FIB, so
 * per-rtableid trees do not exist; the route-to path (pf.c: rtable_iterate)
 * is replaced by the Linux FIB lookup in pfk_xmit.c (task T14). */
#ifndef _PFK_RTABLE_H
#define _PFK_RTABLE_H

#include <linux/types.h>

struct pf_rtentry {
	void		*rt_addr;	/* destination */
	unsigned int	 rt_ifidx;	/* egress ifindex */
	unsigned int	 rt_flags;
#ifndef RTF_UP
#define	RTF_UP		0x1	/* route usable (OpenBSD net/route.h) */
#endif
	int		 rt_listid;
	int		 rt_labelid;
	void		*rt_ifa;	/* BSD in_ifaddr/in6_ifaddr view */
};

int		rtable_exists(unsigned int rtableid);
struct pf_rtentry *rtable_iterate(struct pf_rtentry *rt);

#endif
