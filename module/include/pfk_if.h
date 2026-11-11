/*
 * PF-on-Linux: ifnet/address registry — OpenBSD `struct ifnet`/`in_ifaddr`
 * views over Linux net_device + in_ifaddr lists (ISC 2026).
 */
#ifndef _PFK_IF_H
#define _PFK_IF_H

struct pfk_ifaddr {
	struct pfk_ifaddr	*ifa_next;
	struct pfk_ifnet	*ifa_ifp;
	int			ifa_refcnt;
	int			ifa_flags;	/* BSD IFA_* */
	struct sockaddr_storage	ifa_addr_storage;	/* BSD-ish sockaddr */
	struct sockaddr		*ifa_addr;	/* -> storage */
	struct sockaddr		*ifa_mask;
	struct net_device	*dev;
	__be32			addr4;
	struct in6_addr		addr6;
	int			family;		/* AF_INET/AF_INET6 */
	int			scope;		/* link/site for v6 */
};

struct pfk_ifqueue;	/* M5 */

struct pfk_ifnet {
	char			if_xname[IFNAMSIZ];
	int			if_index;
	struct net_device	*dev;
	int			if_flags;	/* BSD-mapped IFF_* */
	int			if_xflags;	/* BSD IFFX_* */
	int			if_type;	/* IFT_* */
	int			if_mtu;
	void			*if_softc;
	struct pfk_ifaddr	*if_addrlist;	/* refreshed snapshot */
	struct pf_kif		*if_pf_kif;	/* pf_if glue */
	struct pfk_ifqueue	*if_snd;	/* M5 */
	void			*if_pf_rtable;	/* unused (rt tables NG) */
	int			if_rdomain;	/* always 0 (NG), kept for ABI */
	struct rcu_head		rcu;
	struct list_head	link;
	int			alive;
};

#define if_output		pfk_if_output_placeholder
struct pfk_ifnet *pfk_ifp_byindex(int idx);		/* NULL if gone */
struct pfk_ifnet *pfk_ifp_byname(const char *name);
struct pfk_ifnet *pfk_ifp_bydev(struct net_device *dev);
int	 pfk_if_addrlist_refresh(struct pfk_ifnet *ifp); /* NET_LOCK held */
void	 pfk_if_free(struct pfk_ifnet *ifp);

/* BSD IFF_* bits (OpenBSD values) — distinct from Linux! */
#define PFIFF_UP		0x00000001
#define PFIFF_BROADCAST		0x00000002
#define PFIFF_DEBUG		0x00000004
#define PFIFF_LOOPBACK		0x00000008
#define PFIFF_POINTOPOINT	0x00000010
#define PFIFF_RUNNING		0x00000040
#define PFIFF_PROMISC		0x00000100
#define PFIFF_ALLMULTI		0x00000200
#define PFIFF_MULTICAST		0x00008000
#define PFIFF_RESOLVE		0x02000000
#define PFIFF_IPV4		0x10000000
#define PFIFF_IPV6		0x20000000

/* NOTE: BSD IFF_* bit values are PFIFF_*; staged port files are sed'ed
 * IFF_* → PFIFF_* (IFF_ macros must NEVER alias linux netdev flags).
 * BSD IFA_* */
#define IFA_BROADCAST_	0x02
#define IFA_PRIMARY_	0x04
#define IFA_SECONDARY_	0x08

struct mbuf_list { struct mbuf *ml_first, *ml_last; int ml_len; };

/* egress interface (default-route dev), maintained on fib/net events */
struct pfk_ifnet *pfk_egress_if(void);
void pfk_egress_refresh(void);

int  pfk_if_init(void);
void pfk_if_exit(void);
void pfk_if_refresh(struct pfk_ifnet *ifp);
struct pfk_ifnet *pfk_if_first(void);
struct pfk_ifnet *pfk_if_next(struct pfk_ifnet *ifp);
struct pfk_ifnet *pfk_if_next_from(int *idp);
struct net_device *ip_rt_get_default_dev(struct net *net);

/* xmit API used by pf_route/pf output (implemented in pfk_xmit.c) */
int if_output_tso(struct pfk_ifnet *ifp, struct mbuf **mp,
	struct sockaddr *dst, void *rt, int mtu);
int if_output_ml(struct pfk_ifnet *ifp, struct mbuf_list *ml,
	struct sockaddr *dst, void *rt);
int if_enqueue(struct pfk_ifnet *ifp, struct mbuf *m);

/* addr lookups (ifa_ifwithaddr, in_ifwithaddr etc.) */
struct pfk_ifaddr *pfk_ifwithaddr(const struct sockaddr *sa);
struct pfk_ifaddr *pfk_ifwithroute(int family, const struct sockaddr *dst,
	const struct sockaddr *gate, struct pfk_ifnet **ifp_out);

/* mib-ish status for pfctl DIOCGIFSTATS */
struct pfk_if_data { unsigned long long ifi_ibytes, ifi_obytes; };
void pfk_if_stats(struct pfk_ifnet *ifp, struct pfk_if_data *out);

#endif /* _PFK_IF_H */
