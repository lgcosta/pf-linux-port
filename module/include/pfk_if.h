/*
 * PF-on-Linux: ifnet/address registry — OpenBSD `struct ifnet`/`in_ifaddr`
 * views over Linux net_device + in_ifaddr lists (ISC 2026).
 */
#ifndef _PFK_IF_H
#define _PFK_IF_H

struct ifg_list;

struct pfk_ifnet {
	char			if_xname[IFNAMSIZ];
	int			if_index;
	struct net_device	*dev;
	int			if_flags;	/* BSD-mapped IFF_* */
	int			if_xflags;	/* BSD IFFX_* */
	int			if_type;	/* IFT_* */
	int			if_mtu;
	void			*if_softc;
	TAILQ_HEAD(ifa_list, ifaddr) if_addrlist;	/* BSD interface addresses */
	TAILQ_HEAD(, ifg_list)	 if_groups;	/* BSD interface groups */
	void			*if_pf_kif;	/* pf_if glue (BSD: caddr_t) */
	struct ifqueue		 if_snd;	/* transmit queue (ifq) */
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

/* BSD mbuf_list helper */
static inline struct mbuf *
ml_dequeue(struct mbuf_list *ml)
{
	struct mbuf *m;

	if (!ml || !ml->ml_first)
		return NULL;
	m = ml->ml_first;
	ml->ml_first = m->m_next;
	if (!ml->ml_first)
		ml->ml_last = NULL;
	ml->ml_len--;
	m->m_next = NULL;
	return m;
}


/* BSD <net/if_var.h> mbuf_list helpers (net/if_var.h ml_*) */
static inline void
ml_init(struct mbuf_list *ml)
{
	ml->ml_first = ml->ml_last = NULL;
	ml->ml_len = 0;
}

static inline void
ml_enqueue(struct mbuf_list *ml, struct mbuf *m)
{
	m->m_next = NULL;
	if (ml->ml_last == NULL)
		ml->ml_first = m;
	else
		ml->ml_last->m_next = m;
	ml->ml_last = m;
	ml->ml_len++;
}

static inline struct mbuf *
ml_dequeue_tail(struct mbuf_list *ml)
{
	struct mbuf *m, *prev = NULL;

	if (ml->ml_first == NULL)
		return NULL;
	for (m = ml->ml_first; m->m_next; m = m->m_next)
		prev = m;
	if (prev == NULL)			/* single element */
		ml->ml_first = NULL;
	else
		prev->m_next = NULL;
	ml->ml_last = prev;
	ml->ml_len--;
	m->m_next = NULL;
	return m;
}

/* append every packet in m2 to m1 (m2 becomes empty) */
static inline void
ml_enlist(struct mbuf_list *m1, struct mbuf_list *m2)
{
	if (m2->ml_first == NULL)
		return;
	if (m1->ml_last == NULL)
		m1->ml_first = m2->ml_first;
	else
		m1->ml_last->m_next = m2->ml_first;
	m1->ml_last = m2->ml_last;
	m1->ml_len += m2->ml_len;
	m2->ml_first = m2->ml_last = NULL;
	m2->ml_len = 0;
}

static inline unsigned int
ml_len(const struct mbuf_list *ml)
{
	return (unsigned int)ml->ml_len;
}

static inline int
ml_empty(const struct mbuf_list *ml)
{
	return ml->ml_first == NULL;
}

void	ml_purge(struct mbuf_list *ml);	/* compat/pfk_mbuf.c (needs m_freem) */

#define MBUF_LIST_FIRST(_ml)		((_ml)->ml_first)
#define MBUF_LIST_INITIALIZER()		{ NULL, NULL, 0 }

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

/* Notification for PF engine (pfi attach/detach), called under NET_LOCK:
 * added=1 new interface (after populate), added=0 interface going away. */
void pfk_if_set_event_cb(void (*cb)(int added, struct pfk_ifnet *ifp));
/* Iterate all live interfaces with cb(ifp) under NET_LOCK-read. */
void pfk_if_iterate(void (*cb)(struct pfk_ifnet *ifp));

/* xmit API used by pf_route/pf output (implemented in pfk_xmit.c).
 * PFLINUX: `dst` is void * so BSD accessor macros (sintosa/sin6tosa,
 * which keep the sockaddr_in/sockaddr_in6 type for field access) can be
 * passed directly, as staged pf.c does. */
int if_output_tso(struct pfk_ifnet *ifp, struct mbuf **mp,
	void *dst, void *rt, int mtu);
int if_output_ml(struct pfk_ifnet *ifp, struct mbuf_list *ml,
	void *dst, void *rt);
int if_enqueue(struct pfk_ifnet *ifp, struct mbuf *m);
int pfk_if_output(struct pfk_ifnet *ifp, struct mbuf *m, void *dst, void *rt);

/* addr lookups (ifa_ifwithaddr, in_ifwithaddr etc.) */
struct ifaddr *pfk_ifwithaddr(const struct sockaddr *sa);
struct ifaddr *pfk_ifwithroute(int family, const struct sockaddr *dst,
	const struct sockaddr *gate, struct pfk_ifnet **ifp_out);

/* mib-ish status for pfctl DIOCGIFSTATS */
struct pfk_if_data { unsigned long long ifi_ibytes, ifi_obytes; };
void pfk_if_stats(struct pfk_ifnet *ifp, struct pfk_if_data *out);

#endif /* _PFK_IF_H */
