/*
 * PF-on-Linux transmit + return-packet helpers (ISC 2026).
 * if_output_tso/ml + if_enqueue mirror OpenBSD calls; route-to uses them
 * with a gateway dst (see pf_route.c, T14). Return errors use Linux
 * ipstack helpers (exported: icmp_send; P1 exports ip_route_output_key,
 * ip6_route_output, tcp_v4_send_reset if needed).
 */
#include <pfk.h>
#include <pfk_api.h>

static struct dst_entry *
pfk_route4(__be32 daddr, __be32 saddr, int oif)
{
	struct flowi4 fl4 = { };
	struct rtable *rt;

	fl4.daddr = daddr;
	fl4.saddr = saddr;
	if (oif)
		fl4.flowi4_oif = oif;
	rt = ip_route_output_key(&init_net, &fl4);
	if (IS_ERR(rt))
		return NULL;
	return &rt->dst;
}

/*
 * Forced nexthop (route-to/dup-to/reply-to).  Linux resolves the neighbour
 * from the *packet's* destination unless the route says it goes through a
 * gateway (ip_neigh_for_gw() only honours rt_gw4 when rt_gw_family is set),
 * which would ARP the final host on the gateway's link.  Look the gateway up
 * as its own host route and mark it as the nexthop - the same pattern OVS
 * uses after its own FIB lookups.
 */
static struct dst_entry *
pfk_route4_nh(__be32 gw, int oif)
{
	struct rtable *rt = (struct rtable *)pfk_route4(gw, 0, oif);

	if (rt == NULL)
		return NULL;
	rt->rt_gw_family = AF_INET;
	rt->rt_gw4 = gw;
	return &rt->dst;
}

#if IS_ENABLED(CONFIG_IPV6)
static struct dst_entry *
pfk_route6(const struct in6_addr *daddr, const struct in6_addr *saddr, int oif)
{
	struct flowi6 fl6 = { };
	struct dst_entry *d;

	fl6.daddr = *daddr;
	if (saddr)
		fl6.saddr = *saddr;
	if (oif)
		fl6.flowi6_oif = oif;
	d = ip6_route_output(&init_net, NULL, &fl6);
	if (!d || d->error) {
		if (d)
			dst_release(d);
		return NULL;
	}
	return d;
}
#endif

/* xmit: BSD semantics — caller hands fully-formed IP packet + dst sock.
 * rt arg is OpenBSD's cached route (opaque to us). Returns 0/errno. */
int
if_output_tso(struct pfk_ifnet *ifp, struct mbuf **mp, void *dst,
	      void *rt, int mtu)
{
	struct mbuf *m = *mp;
	struct sk_buff *skb = m_to_skb(m);
	struct dst_entry *d = NULL;
	struct sockaddr *sa = (struct sockaddr *)dst;
	int ret = 0;

	if (!pskb_may_pull(skb, sizeof(struct pf_ip))) {
		ret = EINVAL;
		goto drop;
	}
	if (skb_ensure_writable(skb, 0)) {
		ret = ENOBUFS;
		goto drop;
	}
	switch (sa->sa_family) {
	case AF_INET: {
		__be32 gw = 0;

		/* if route was forced (route-to), dst holds gw sockaddr
		 * stored in sin_addr by pf_route; normal case dst==final */
		gw = sintosa(dst)->sin_addr.s_addr;
		/* PFLINUX: look the route up by destination only.  Passing the
		 * packet's source (a transit address, not local to us) makes
		 * ip_route_output_key fail with ENETUNREACH; ip_output keeps
		 * the header source untouched. */
		d = pfk_route4_nh(gw, ifp->if_index);
		if (!d) {
			ret = EHOSTUNREACH;
			goto drop;
		}
		skb_dst_set(skb, d);
		break;
	}
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6: {
		const struct in6_addr *gw = &sin6tosa(dst)->sin6_addr;

		if (!d)
			/* PFLINUX: destination-only, as for IPv4 */
			d = pfk_route6(gw, NULL, ifp->if_index);
		if (!d) {
			ret = EHOSTUNREACH;
			goto drop;
		}
		skb_dst_set(skb, d);
		skb->protocol = htons(ETH_P_IPV6);
		break;
	}
#endif
	default:
		ret = EAFNOSUPPORT;
		goto drop;
	}

	skb->dev = skb_dst(skb)->dev;
	skb_reset_network_header(skb);
	if (skb->protocol == 0)
		skb->protocol = htons(ETH_P_IP);
	/* push through network layer exactly like OpenBSD's if_output:
	 * dst->output = ip_output/ip6_output (headers already built) */
	if (!netif_running(skb->dev)) {
		ret = ENETDOWN;
		goto drop_dst_nofree;
	}
	/*
	 * The stack's output path always consumes exactly one skb reference.
	 * Take our own so the caller's references (the hook queue ref and the
	 * mbuf wrapper ref) stay balanced - without this the work queue's
	 * cleanup double-frees the skb and corrupts the slab.
	 */
	/*
	 * pf_route() has already evaluated the OUT ruleset (it calls
	 * pf_test(PF_OUT) itself, exactly like OpenBSD's ip_forward).  Mark the
	 * packet so our POST_ROUTING hook does not run the out ruleset a second
	 * time - that re-match hits the state, re-enters pf_route and recurses
	 * (OpenBSD stops this with PF_TAG_PROCESSED carried in the mbuf).
	 */
	skb->mark |= PFK_DONE_OUT;
	/*
	 * The port stashes its mbuf pointer in skb->cb; the IP stack uses the
	 * same bytes as its own control block (IPCB).  Hand the packet to the
	 * stack with a clean cb, otherwise ip_output() reads our pointer as
	 * frag_max_size/flags and the packet is silently lost.
	 */
	memset(skb->cb, 0, sizeof(skb->cb));
	skb_get(skb);
	ret = skb_dst(skb)->output(&init_net, NULL, skb);
	/* output consumed skb (and wrapper); avoid double free */
	if (ret == 0) {
		m->skb = NULL;		/* ownership moved */
		m_freem(m);
		*mp = NULL;
	}
	return ret;

drop_dst_nofree:
	ret = ENETDOWN;
drop:
	if (m) {
		m_freem(m);
		*mp = NULL;
	}
	if (ret == ENOBUFS || ret == ENETDOWN || ret == EHOSTUNREACH)
		pfk_if_congestion();
	return ret;
}

int
if_output_ml(struct pfk_ifnet *ifp, struct mbuf_list *ml,
	     void *dst, void *rt)
{
	struct mbuf *m;
	int err = 0, n;

	while ((m = ml->ml_first) != NULL) {
		ml->ml_first = m->m_next;
		m->m_next = NULL;
		n = if_output_tso(ifp, &m, dst, rt, ifp->if_mtu);
		if (n)
			err = n;
	}
	ml->ml_len = 0;
	return err;
}

int
if_enqueue(struct pfk_ifnet *ifp, struct mbuf *m)
{
	/* PFLINUX M1-M4: no qdisc attached → direct xmit via same path;
	 * M5 replaces this with the pfq enqueue (spec §4.3). */
	struct sockaddr sa = { .sa_family = AF_INET };
	struct mbuf *mp = m;

	if (m_to_skb(m)->protocol == htons(ETH_P_IPV6))
		sa.sa_family = AF_INET6;
	if_output_tso(ifp, &mp, &sa, NULL, ifp->if_mtu);
	if (mp == NULL)
		return 0;
	m_freem(mp);
	return ENOBUFS;
}

/*
 * Default-route egress *interface index* for pfk_egress_refresh().
 *
 * PFLINUX: only the index is returned, never the device pointer: the route
 * is released before the caller uses the value, so the device must not be
 * touched afterwards.  The caller must run this in process context and
 * without rtnl held - a FIB lookup can take rtnl itself, and
 * register_netdevice_notifier() calls its notifiers *under* rtnl_lock
 * (deadlock: see pfk_netdev_event()).
 */
int
ip_rt_get_default_ifindex(struct net *net)
{
	struct flowi4 fl4 = { };
	struct rtable *rt;
	int idx = 0;

	fl4.daddr = htonl(0x08080808);
	fl4.flowi4_oif = 0;
	rt = ip_route_output_key(net, &fl4);
	if (IS_ERR(rt))
		return (0);
	if (rt->dst.dev != NULL)
		idx = rt->dst.dev->ifindex;
	ip_rt_put(rt);
	return (idx);
}

/* ---------------- return errors ---------------- */
u_int16_t
pfk_in_cksum(const void *data, int len)
{
	return csum_fold(csum_partial(data, len, 0));
}

void
pfk_send_icmp_error(int family, struct sk_buff *skb_in, int type, int code,
	__be32 info)
{
	if (family == AF_INET) {
		icmp_send(skb_in, type, code, info);
		return;
	}
#if IS_ENABLED(CONFIG_IPV6)
	pr_warn_once("pfk: ICMPv6 error path pending P1 export (type %d)\n", type);
#endif
}

/* NOTE: TCP RST generation is done by PF itself (pf.c builds the segment
 * and calls if_output_tso), so no kernel tcp_v4_send_reset export is
 * needed — matches OpenBSD's pf_send_rst() design. */

/*
 * pfk_if_output(ifp, m, dst, rt): BSD if_output() equivalent used by the
 * ported PF code (route-to / refragment / divert paths).
 */
int
pfk_if_output(struct pfk_ifnet *ifp, struct mbuf *m, void *dst,
    void *rt)
{
	struct mbuf *mp = m;
	int err;

	err = if_output_tso(ifp, &mp, dst, rt, ifp->if_mtu);
	if (mp)
		m_freem(mp);
	return err;
}

/*
 * Transmit one queued packet.  Called from ifq_start() - i.e. from the pf
 * work queue or from a scheduler's deferred timeout - and always consumes the
 * mbuf.  Returns 0 (keep draining); a scheduler that wants to defer does so by
 * simply not returning a packet from ifqop_deq_begin().
 */
int
pfk_ifq_transmit(struct ifqueue *ifq, struct mbuf *m)
{
	struct sk_buff *skb = m_to_skb(m);
	struct dst_entry *d;

	(void)ifq;
	if (skb == NULL) {
		m_freem(m);
		return 0;
	}
	d = skb_dst(skb);
	if (d == NULL || IS_ERR(d) || d->dev == NULL || !netif_running(d->dev)) {
		m_freem(m);
		return 0;
	}
	skb->dev = d->dev;
	skb_reset_network_header(skb);
	if (skb->protocol == 0)
		skb->protocol = htons(ip_hdr(skb)->protocol == IPPROTO_IPV6 ?
		    ETH_P_IPV6 : ETH_P_IP);
	/* the stack owns skb->cb (IPCB) from here; pf already tested it */
	memset(skb->cb, 0, sizeof(skb->cb));
	skb->mark |= PFK_DONE_OUT;
	skb_get(skb);		/* the output path consumes one reference */
	m->skb = NULL;		/* the mbuf's reference moves to the stack */
	m_freem(m);
	d->output(&init_net, NULL, skb);
	return 0;
}
