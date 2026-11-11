/*
 * PF-on-Linux BSD IPv4/IPv6 layer used by pf.c (ISC 2026).
 *
 * OpenBSD's netinet entry points that PF calls directly for the AFRT
 * (64<->4 translation), route-to, RST/error-return and state-ICMP paths.
 * The staging seds rename ip_output/ip_forward/ip6_output to pfk_* (they
 * clash with Linux declarations); ip_send/ip6_send/icmp_error/ip_fragment
 * have no Linux declaration and keep their BSD names.
 *
 * All of these CONSUME the mbuf (OpenBSD semantics), matching the callers
 * in pf.c which set pd->m = NULL right after.
 */
#include <pfk.h>

static struct rtable *
pfk_ip4_route(__be32 daddr, __be32 saddr)
{
	struct flowi4 fl4 = { };
	struct rtable *rt;

	fl4.daddr = daddr;
	fl4.saddr = saddr;
	rt = ip_route_output_key(&init_net, &fl4);
	if (IS_ERR(rt))
		return NULL;
	return rt;
}

/* one's complement sum over an mbuf range (shim: linear sk_buff) */
u_int16_t
in_cksum(struct mbuf *m, int len)
{
	struct sk_buff *skb = m_to_skb(m);

	if (!skb || !pskb_may_pull(skb, len))
		return 0xffff;
	return pfk_in_cksum(skb->data, len);
}

/*
 * in4_cksum(m, nxt, off, len): transport checksum including the IPv4
 * pseudo-header.  off/len are relative to the packet start (the IP hdr).
 * Returns the one's complement sum; a valid packet computes to 0, and a
 * value to be STORED is (sum == 0 ? 0xffff : ~sum) style — callers do
 * their own fixups; we return the raw folded sum like OpenBSD.
 */
u_int16_t
in4_cksum(struct mbuf *m, __u8 nxt, u_int32_t off, u_int32_t len)
{
	struct sk_buff *skb = m_to_skb(m);
	struct pf_ip *ip;
	struct {
		__be32	src, dst;
		__u8	zero, proto;
		__be16	len;
	} ph;
	__wsum base;

	if (!skb || !pskb_may_pull(skb, off + len) ||
	    !pskb_may_pull(skb, sizeof(*ip)))
		return 0xffff;
	ip = (struct pf_ip *)skb->data;

	ph.src = ip->ip_src.s_addr;
	ph.dst = ip->ip_dst.s_addr;
	ph.zero = 0;
	ph.proto = nxt;
	ph.len = htons(len);
	base = csum_partial(&ph, sizeof(ph), 0);
	return csum_fold(csum_partial(skb->data + off, len, base));
}

u_int16_t
in6_cksum(struct mbuf *m, __u8 nxt, u_int32_t off, u_int32_t len)
{
	struct sk_buff *skb = m_to_skb(m);
	struct pf_ip6_hdr *ip6;
	__wsum base;

	if (!skb || !pskb_may_pull(skb, sizeof(*ip6)) ||
	    !pskb_may_pull(skb, off + len))
		return 0xffff;
	ip6 = (struct pf_ip6_hdr *)skb->data;

	base = csum_partial(ip6->ip6_src.s6_addr, sizeof(ip6->ip6_src), 0);
	base = csum_add(base, csum_partial(ip6->ip6_dst.s6_addr,
	    sizeof(ip6->ip6_dst), 0));
	base = csum_add(base, (__force __u32)htonl(len));
	base = csum_add(base, (__force __u32)htonl(nxt));
	return csum_fold(csum_partial(skb->data + off, len, base));
}

/* recompute the IPv4 header checksum in place */
void
in_hdr_cksum_out(struct mbuf *m, void *ro)
{
	struct sk_buff *skb = m_to_skb(m);
	struct pf_ip *ip;
	int hlen;

	(void)ro;
	if (!skb || !pskb_may_pull(skb, sizeof(*ip)))
		return;
	ip = (struct pf_ip *)skb->data;
	hlen = ip->ip_hl << 2;
	if (hlen < (int)sizeof(*ip) || !pskb_may_pull(skb, hlen))
		return;
	ip->ip_sum = 0;
	ip->ip_sum = csum_fold(csum_partial(skb->data, hlen, 0));
	pfk_mbuf_refresh(m);
}

/* compute a pending transport checksum requested via csum_flags */
static void
pfk_proto_cksum_out(struct mbuf *m)
{
	struct sk_buff *skb = m_to_skb(m);
	struct pf_ip *ip;
	int hlen;

	if (!skb || !(m->m_pkthdr.csum_flags &
	    (M_TCP_CSUM_OUT | M_UDP_CSUM_OUT)))
		return;
	if (!pskb_may_pull(skb, sizeof(*ip)))
		return;
	ip = (struct pf_ip *)skb->data;
	hlen = ip->ip_hl << 2;
	if (ip->ip_p == IPPROTO_TCP) {
		__be16 *sum;
		u_int16_t s;

		if (!pskb_may_pull(skb, hlen + 20))
			return;
		sum = (__be16 *)(skb->data + hlen + 16);	/* th_sum */
		*sum = 0;
		s = in4_cksum(m, IPPROTO_TCP, hlen, ntohs(ip->ip_len) - hlen);
		*sum = s == 0 ? (__force __be16)0xffff : (__force __be16)~s;
	} else if (ip->ip_p == IPPROTO_UDP) {
		__be16 *sum;
		u_int16_t s;

		if (!pskb_may_pull(skb, hlen + 8))
			return;
		sum = (__be16 *)(skb->data + hlen + 6);	/* uh_sum */
		*sum = 0;
		s = in4_cksum(m, IPPROTO_UDP, hlen, ntohs(ip->ip_len) - hlen);
		if (s == 0)
			s = 0xffff;	/* RFC 768: zero means "unused" */
		*sum = (__force __be16)s;
	}
	pfk_mbuf_refresh(m);
}

/*
 * pfk_ip_output(m, opt, ro, flags, imcb, so, flowid) — BSD ip_output().
 * PFLINUX: option processing (opt/record-route), IPsec metadata (imcb) and
 * socket accounting (so) are not modelled; flags IP_FORWARDING /
 * IP_ALLOWBROADCAST / IP_REDIRECT / *_IPSEC are accepted.  The packet is
 * routed by its own destination header and injected into the stack output
 * path (exactly what OpenBSD's ip_output ends up doing at if_output).
 * Consumes m.  Returns 0 or errno.
 */
int
pfk_ip_output(struct mbuf *m, void *opt, void *ro, u_int flags, void *imcb,
    void *so, u_int flowid)
{
	struct sk_buff *skb = m ? m_to_skb(m) : NULL;
	struct pf_ip *ip;
	struct rtable *rt;

	(void)opt; (void)ro; (void)flags; (void)imcb; (void)so; (void)flowid;
	if (!skb)
		return EINVAL;
	if (!pskb_may_pull(skb, sizeof(*ip))) {
		m_freem(m);
		return EMSGSIZE;
	}
	ip = (struct pf_ip *)skb->data;

	pfk_proto_cksum_out(m);
	in_hdr_cksum_out(m, NULL);
	skb_reset_network_header(skb);
	skb->protocol = htons(ETH_P_IP);

	rt = pfk_ip4_route(ip->ip_dst.s_addr, ip->ip_src.s_addr);
	if (!rt) {
		/* BSD: no route while forwarding → ICMP host unreachable */
		if (flags & IP_FORWARDING) {
			/* re-derive a mutable copy for quoting */
			struct sk_buff *q;

			q = skb_copy(skb, GFP_ATOMIC);
			if (q) {
				icmp_error(m_dtom(q), ICMP_UNREACH,
				    ICMP_UNREACH_HOST, 0, 0);
			}
		} else {
			struct sk_buff *q = skb_copy(skb, GFP_ATOMIC);

			if (q)
				icmp_error(m_dtom(q), ICMP_UNREACH,
				    ICMP_UNREACH_HOST, 0, 0);
		}
		m_freem(m);
		pfk_if_congestion();
		return EHOSTUNREACH;
	}
	skb_dst_set(skb, &rt->dst);
	skb->dev = rt->dst.dev;
	if (!netif_running(skb->dev)) {
		m->skb = NULL;
		m_freem(m);
		kfree_skb(skb);
		return ENETDOWN;
	}
	m->skb = NULL;		/* output consumes the skb */
	m_freem(m);
	skb_dst(skb)->output(&init_net, NULL, skb);
	return 0;
}

/*
 * ip_send(m): route + transmit a complete IPv4 packet to its own
 * destination (OpenBSD: used by PF to send RST/error packets).
 * Consumes m.
 */
void
ip_send(struct mbuf *m)
{
	if (!m)
		return;
	(void)pfk_ip_output(m, NULL, NULL, 0, NULL, NULL, 0);
}

/*
 * pfk_ip_forward(m, ifp, tag, flags) — BSD ip_forward(): ttl-- , recheck,
 * forward toward the packet's destination.
 * PFLINUX: ICMP redirect generation (IP_REDIRECT flag) is NOT implemented;
 * the port lets Linux routing emit redirects.  IP_FORWARDING_IPSEC has no
 * meaning in the port (no kernel IPsec coupling) and is ignored.
 * Consumes m.
 */
void
pfk_ip_forward(struct mbuf *m, struct pfk_ifnet *ifp, void *tag, u_int flags)
{
	struct sk_buff *skb = m ? m_to_skb(m) : NULL;
	struct pf_ip *ip;

	(void)ifp; (void)tag;
	if (!skb)
		return;
	if (!pskb_may_pull(skb, sizeof(*ip))) {
		m_freem(m);
		return;
	}
	ip = (struct pf_ip *)skb->data;
	if (skb_ensure_writable(skb, sizeof(*ip))) {
		m_freem(m);
		return;
	}
	ip = (struct pf_ip *)skb->data;

	if (ip->ip_ttl <= IPTTLDEC) {
		struct sk_buff *q = skb_copy(skb, GFP_ATOMIC);

		if (q)
			icmp_error(m_dtom(q), ICMP_TIMXCEED,
			    ICMP_TIMXCEED_INTRANS, 0, 0);
		m_freem(m);
		return;
	}
	ip->ip_ttl -= IPTTLDEC;
	in_hdr_cksum_out(m, NULL);

	(void)pfk_ip_output(m, NULL, NULL, flags | IP_FORWARDING, NULL, NULL,
	    0);
}

/*
 * ip_fragment(m, ml, ifp, mtu): split a too-large IPv4 packet into
 * MTU-sized fragments appended to ml; consumes m (OpenBSD semantics:
 * always consumed).  Returns 0 or errno.
 */
int
ip_fragment(struct mbuf *m, struct mbuf_list *ml, struct pfk_ifnet *ifp,
    int mtu)
{
	struct pf_ip *ip;
	int hlen, len, dlen, flen, off, error = 0;
	__be16 ipid;
	int maxfacilen;

	if (!m || !ml)
		return EINVAL;
	ip = mtod(m, struct pf_ip *);
	if (m->m_len < (int)sizeof(*ip)) {
		m_freem(m);
		return EMSGSIZE;
	}
	hlen = ip->ip_hl << 2;
	len = ntohs(ip->ip_len);
	if (len <= mtu) {
		/* single piece */
		if (ml->ml_last)
			ml->ml_last->m_next = m;
		else
			ml->ml_first = m;
		ml->ml_last = m;
		ml->ml_len++;
		return 0;
	}
	dlen = len - hlen;
	flen = (mtu - hlen) & ~7;
	if (flen < 8) {
		m_freem(m);
		return EMSGSIZE;
	}
	ipid = ip->ip_id;
	maxfacilen = dlen;

	off = 0;
	while (off < maxfacilen) {
		struct mbuf *fm;
		struct pf_ip *fi;
		int thislen = maxfacilen - off;
		int totlen;

		if (thislen > flen)
			thislen = flen;
		fm = m_gethdr(M_NOWAIT, MT_HEADER);
		if (!fm) {
			error = ENOBUFS;
			break;
		}
		totlen = hlen + thislen;
		if (skb_put(fm->skb, totlen) == NULL) {
			m_freem(fm);
			error = ENOBUFS;
			break;
		}
		memcpy(fm->m_data, m->m_data, hlen);
		memcpy(fm->m_data + hlen, m->m_data + hlen + off, thislen);
		fi = mtod(fm, struct pf_ip *);
		fi->ip_len = htons(totlen);
		fi->ip_id = ipid;
		fi->ip_off = htons(off >> 3);
		if (off + thislen < maxfacilen)
			fi->ip_off |= htons(IP_MF);
		fi->ip_sum = 0;
		pfk_mbuf_refresh(fm);
		in_hdr_cksum_out(fm, NULL);

		if (ml->ml_last)
			ml->ml_last->m_next = fm;
		else
			ml->ml_first = fm;
		ml->ml_last = fm;
		ml->ml_len++;
		off += thislen;
	}
	m_freem(m);
	return error;
}

/*
 * icmp_error(n, type, code, dest, mtu): build and send an ICMP error
 * quoting the offending packet n; consumes n (OpenBSD semantics).
 */
void
icmp_error(struct mbuf *n, u_int type, u_int code, u_int dest, u_int mtu)
{
	struct sk_buff *skb = n ? m_to_skb(n) : NULL;
	struct pf_ip *oip;
	struct mbuf *m;
	struct pf_ip *ip;
	struct pf_icmp *ic;
	int hlen, qlen, len;
	__be32 rsaddr;

	if (!skb || !pskb_may_pull(skb, sizeof(*oip))) {
		if (n)
			m_freem(n);
		return;
	}
	oip = (struct pf_ip *)skb->data;
	hlen = oip->ip_hl << 2;

	/* quote: ip header + at least 8 bytes, fit into RFC 792 minimum */
	qlen = min_t(int, ntohs(oip->ip_len), 576 - 20 - 8);
	if (qlen < hlen + 8)
		qlen = min(hlen + 8, (int)skb->len);

	len = 20 + 8 + qlen;
	m = m_gethdr(M_DONTWAIT, MT_HEADER);
	if (!m) {
		m_freem(n);
		return;
	}
	if (skb_put(m->skb, len) == NULL) {
		m_freem(m);
		m_freem(n);
		return;
	}
	ip = mtod(m, struct pf_ip *);
	ic = (struct pf_icmp *)(ip + 1);
	memset(ip, 0, 20);
	ip->ip_v = 4;
	ip->ip_hl = 5;
	ip->ip_len = htons(len);
	ip->ip_id = ip_randomid();
	ip->ip_off = htons(atomic_load_int(&ip_mtudisc) ? IP_DF : 0);
	ip->ip_ttl = atomic_load_int(&ip_defttl);
	ip->ip_p = IPPROTO_ICMP;
	ip->ip_dst = oip->ip_src;	/* answer to the offender */
	/* source: address of our route egress interface (mirrors BSD,
	 * which fills ip_src from the route/interface).  Linux 7.3's
	 * struct rtable no longer caches rt_src, so consult the PF
	 * interface registry for the device's first IPv4 address. */
	{
		struct rtable *rt = pfk_ip4_route(oip->ip_src.s_addr, 0);
		struct pfk_ifnet *ifp;

		rsaddr = 0;
		if (rt) {
			ifp = rt->dst.dev ?
			    pfk_ifp_byindex(rt->dst.dev->ifindex) : NULL;
			if (ifp) {
				struct ifaddr *ifa;

				TAILQ_FOREACH(ifa, &ifp->if_addrlist, ifa_list) {
					if (ifa->ifa_addr &&
					    ifa->ifa_addr->sa_family == AF_INET) {
						rsaddr = sintosa(ifa->ifa_addr)->sin_addr.s_addr;
						break;
					}
				}
			}
			dst_release(&rt->dst);
		}
	}
	ip->ip_src.s_addr = rsaddr;

	ic->icmp_type = type;
	ic->icmp_code = code;
	ic->icmp_cksum = 0;
	if (type == ICMP_UNREACH && code == ICMP_UNREACH_NEEDFRAG) {
		ic->icmp_nextmtu = htons(((__u16)mtu));
	} else {
		ic->icmp_void = 0;
		if (dest)
			ic->icmp_gwaddr = (__force __be32)dest;
	}
	memcpy(ic + 1, (char *)oip, qlen);
	{
		__wsum w = csum_partial(ic, 8 + qlen, 0);

		ic->icmp_cksum = csum_fold(w);
	}
	m->m_pkthdr.len = m->m_len = len;
	pfk_mbuf_refresh(m);
	m->m_pkthdr.pf.flags |= PF_TAG_GENERATED;

	if (n)
		m_freem(n);
	ip_send(m);
}

/*
 * pfk_ip6_output(m, opt, ro, flags, so, flowid) — BSD ip6_output() for
 * IPv6 packets with a complete header.  PFLINUX: same reductions as
 * pfk_ip_output; consumes m; returns 0/errno.
 */
int
pfk_ip6_output(struct mbuf *m, void *opt, void *ro, u_int flags, void *so,
    void *flowid)
{
	struct sk_buff *skb = m ? m_to_skb(m) : NULL;
	struct pf_ip6_hdr *ip6;
	struct flowi6 fl6 = { };
	struct dst_entry *d;

	(void)opt; (void)ro; (void)flags; (void)so; (void)flowid;
	if (!skb)
		return EINVAL;
	if (!pskb_may_pull(skb, sizeof(*ip6))) {
		m_freem(m);
		return EMSGSIZE;
	}
	ip6 = (struct pf_ip6_hdr *)skb->data;
	if (m->m_pkthdr.csum_flags & (M_TCP_CSUM_OUT | M_UDP_CSUM_OUT)) {
		int plen = ntohs(ip6->ip6_plen);

		if (ip6->ip6_nxt == IPPROTO_TCP && plen >= 20) {
			__be16 *sum = (__be16 *)(skb->data + sizeof(*ip6) + 16);

			*sum = 0;
			*sum = (__force __be16)in6_cksum(m, IPPROTO_TCP,
			    sizeof(*ip6), plen);
		} else if (ip6->ip6_nxt == IPPROTO_UDP && plen >= 8) {
			__be16 *sum = (__be16 *)(skb->data + sizeof(*ip6) + 6);
			u_int16_t s = in6_cksum(m, IPPROTO_UDP, sizeof(*ip6),
			    plen);

			*sum = s == 0 ? 0xffff : (__force __be16)s;
		}
	}
	skb_reset_network_header(skb);
	skb->protocol = htons(ETH_P_IPV6);
	fl6.daddr = ip6->ip6_dst;
	fl6.saddr = ip6->ip6_src;
	d = ip6_route_output(&init_net, NULL, &fl6);
	if (!d || d->error) {
		if (d)
			dst_release(d);
		m_freem(m);
		pfk_if_congestion();
		return EHOSTUNREACH;
	}
	skb_dst_set(skb, d);
	skb->dev = d->dev;
	if (!netif_running(skb->dev)) {
		m->skb = NULL;
		m_freem(m);
		kfree_skb(skb);
		return ENETDOWN;
	}
	m->skb = NULL;
	m_freem(m);
	skb_dst(skb)->output(&init_net, NULL, skb);
	return 0;
}

/* ip6_send(m): BSD convenience — output an IPv6 packet to its dst */
void
ip6_send(struct mbuf *m)
{
	if (!m)
		return;
	(void)pfk_ip6_output(m, NULL, NULL, 0, NULL, 0);
}
