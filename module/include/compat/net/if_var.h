/* PFLINUX compat: <net/if_var.h> — the ifnet view comes from pfk_if.h;
 * the BSD interface-group types are reproduced here (they need the BSD
 * queue macros and the refcnt type). */
#ifndef _PFK_IF_VAR_H
#define _PFK_IF_VAR_H

#include "compat/sys/queue.h"
#include "compat/sys/refcnt.h"

struct ifg_member {
	TAILQ_ENTRY(ifg_member)	 ifgm_next;	/* [N] all members are chained */
	struct pfk_ifnet	*ifgm_ifp;	/* [I] member interface */
};

struct ifg_group {
	char			 ifg_group[IFNAMSIZ];
	u_int			 ifg_refcnt;
	caddr_t			 ifg_pf_kif;
	int			 ifg_carp_demoted;
	TAILQ_HEAD(, ifg_member)	 ifg_members;
	TAILQ_ENTRY(ifg_group)	 ifg_next;
	struct refcnt		 ifg_tmprefcnt;
	TAILQ_ENTRY(ifg_group)	 ifg_tmplist;
};

/* BSD struct ifaddr (net/if_var.h) — the ported PF code walks interface
 * addresses through this type. */
struct pfk_ifnet;

struct ifaddr {
	struct sockaddr	*ifa_addr;	/* address of interface */
	struct sockaddr	*ifa_dstaddr;	/* other end of p-to-p link */
#define	ifa_broadaddr	ifa_dstaddr
	struct sockaddr	*ifa_netmask;	/* used to determine subnet */
	struct pfk_ifnet	*ifa_ifp;	/* back-pointer to interface */
	TAILQ_ENTRY(ifaddr) ifa_list;	/* [N] list of addresses */
	TAILQ_ENTRY(ifaddr) ifa_tmplist;/* [T] temporary list */
	u_int		 ifa_flags;
	struct refcnt	 ifa_refcnt;
	int		 ifa_metric;
};

struct ifg_list {
	struct ifg_group	*ifgl_group;	/* [I] interface group */
	TAILQ_ENTRY(ifg_list)	 ifgl_next;	/* [N] all groups are chained */
};

#endif
