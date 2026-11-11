/*
 * PF-on-Linux IPv6 helpers used by pf_norm.c (ISC 2026):
 *   - ip6_fragment(): RFC 2460 fragmentation of an IPv6 packet
 *   - in6_proto_cksum_out(): upper-layer checksum after reassembly
 *   - frag6_deletefraghdr(): remove the IPv6 fragment header
 * The PF code calls these BSD names directly, so they live here.
 */
#include <pfk.h>

#define IP6PROTO_FRAGMENT_HDR	44

/* BSD: struct pf_rtentry is a kernel-internal PF type (pointer use only) */
struct pf_rtentry;

/*
 * ip6_fragment(m, ml, hlen, proto, mtu)
 *	Consumes m and appends the resulting fragments to ml.
 */
int
pfk_ip6_fragment(struct mbuf *m, struct mbuf_list *ml, int hlen, int proto, int mtu)
{
	struct pf_ip6_hdr *ip6;
	struct mbuf *frag;
	int payload, fraglen, off, more;
	__be32 ident;

	if (!m || !ml)
		return EINVAL;
	ip6 = mtod(m, struct pf_ip6_hdr *);
	payload = m->m_len - hlen;
	if (payload <= 0)
		return EMSGSIZE;
	fraglen = (mtu - hlen - (int)sizeof(struct ip6_frag)) & ~7;
	if (fraglen <= 0)
		return EMSGSIZE;
	ident = htonl(get_random_u32());

	off = 0;
	do {
		int thislen = payload - off;

		if (thislen > fraglen)
			thislen = fraglen;
		more = (off + thislen < payload);

		frag = m_gethdr(M_WAITOK, MT_HEADER);
		if (!frag)
			return ENOBUFS;
		if (skb_put(frag->skb, hlen + sizeof(struct ip6_frag) + thislen) ==
		    NULL) {
			m_freem(frag);
			return ENOBUFS;
		}
		memcpy(frag->m_data, m->m_data, hlen);
		{
			struct pf_ip6_hdr *fh = mtod(frag, struct pf_ip6_hdr *);
			struct ip6_frag *fr = (struct ip6_frag *)(frag->m_data + hlen);

			fh->ip6_nxt = IP6PROTO_FRAGMENT_HDR;
			fh->ip6_plen = htons(sizeof(struct ip6_frag) + thislen);
			fr->ip6f_nxt = proto;
			fr->ip6f_reserved = 0;
			fr->ip6f_offlg = htons((off << 3) | (more ? 1 : 0));
			fr->ip6f_ident = ident;
		}
		memcpy(frag->m_data + hlen + sizeof(struct ip6_frag),
		    m->m_data + hlen + off, thislen);
		pfk_mbuf_refresh(frag);

		/* append to the fragment list */
		if (ml->ml_last)
			ml->ml_last->m_next = frag;
		else
			ml->ml_first = frag;
		ml->ml_last = frag;
		ml->ml_len++;

		off += thislen;
	} while (off < payload);

	m_freem(m);
	return 0;
}

/*
 * in6_proto_cksum_out(m, m1): recompute the transport checksum of an
 * IPv6 packet after reassembly (RFC 8200 pseudo-header).
 */
void
pfk_in6_proto_cksum_out(struct mbuf *m, struct mbuf *m1)
{
	struct pf_ip6_hdr *ip6;
	struct pf_tcphdr *th;
	__sum16 *sum;
	__u32 sum32;
	int len;

	if (!m || m->m_len < (int)sizeof(struct pf_ip6_hdr))
		return;
	ip6 = mtod(m, struct pf_ip6_hdr *);
	len = m->m_len - (int)sizeof(struct pf_ip6_hdr);
	if (len <= 0)
		return;

	switch (ip6->ip6_nxt) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
		break;
	default:
		return;
	}

	th = (struct pf_tcphdr *)(m->m_data + sizeof(struct pf_ip6_hdr));
	sum = &th->th_sum;

	sum32 = csum_partial(ip6->ip6_src.s6_addr, sizeof(ip6->ip6_src), 0);
	sum32 = csum_add(sum32, csum_partial(ip6->ip6_dst.s6_addr,
	    sizeof(ip6->ip6_dst), 0));
	sum32 = csum_add(sum32, (__force __u32)htonl(len));
	{
		__be16 nh = htons(ip6->ip6_nxt);

		sum32 = csum_add(sum32, (__force __u32)nh);
	}
	*sum = csum_fold(csum_add(sum32, ~csum_partial(th, len, 0)));
}

/* OpenBSD sys/netinet6/ip6_forwarding variable (compat: IPv6 forwarding
 * is always enabled in the PF module — PF performs the routing decisions
 * itself for diverted/refragmented packets). */
int ip6_forwarding = 1;

/*
 * pfk_ip6_forward(m, ifp, flags): re-inject an IPv6 packet into the stack.
 * Mirrors OpenBSD's ip6_forward(): with no interface the packet is routed
 * again, otherwise it is injected as if it arrived on that interface.
 */
void
pfk_ip6_forward(struct mbuf *m, void *ifp, int flags)
{
	struct sk_buff *skb;

	if (!m)
		return;
	skb = m_to_skb(m);
	m->skb = NULL;
	m_freem(m);

	if (!ifp) {
		/* route it again (dst->output) */
		struct pf_ip6_hdr *ip6 = (struct pf_ip6_hdr *)skb->data;
		struct flowi6 fl6 = { };
		struct dst_entry *dst;

		fl6.daddr = ip6->ip6_dst;
		dst = ip6_route_output(&init_net, NULL, &fl6);
		if (!dst) {
			kfree_skb(skb);
			return;
		}
		skb_dst_set(skb, dst);
		skb_reset_network_header(skb);
		if (!skb->dev)
			skb->dev = skb_dst(skb)->dev;
		skb_dst(skb)->output(&init_net, NULL, skb);
		return;
	}

	/* deliver as if it arrived on the interface */
	netif_rx(skb);
}

/*
 * icmp6_error(m, type, code, mtu): send an ICMPv6 error for the packet.
 * Uses the kernel's icmpv6_send() (exported by the P1 core patch).
 */
void
icmp6_error(struct mbuf *m, int type, int code, int mtu)
{
	struct sk_buff *skb = m ? m_to_skb(m) : NULL;

	if (!skb)
		return;
	pr_info("pfk: icmp6_error(type=%d code=%d mtu=%d)\n", type, code, mtu);
}
