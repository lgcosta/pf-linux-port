/*
 * PF-on-Linux netfilter hook registration + data path (ISC 2026)
 *
 * BSD netisr pattern (PFLINUX divergence, documented):
 *   hooks NEVER call pf_test() in softirq context — OpenBSD runs input at
 *   IPL_NET with a non-sleeping NET read side, while our compat locks are
 *   sleepable rw_semaphores.  The hook therefore steals the packet onto a
 *   per-CPU-free ordered workqueue and pf_test() runs in process context;
 *   the verdict is applied by re-injecting (netif_rx for input, the routed
 *   output path for transit/local-out) with an skb->mark tag so the
 *   survivor is not re-queued by our own hook.
 *
 * One pf_test pass per BSD call site:
 *   NF_INET_PRE_ROUTING  -> PF_IN   (OpenBSD ip_input)
 *   NF_INET_LOCAL_OUT    -> PF_OUT  (OpenBSD ip_output, locally generated)
 *   NF_INET_POST_ROUTING -> PF_OUT  (OpenBSD ip_output, transit)
 * LOCAL_IN / FORWARD are pass-through (already evaluated).
 */
#include <pfk.h>
#include <pfk_api.h>
#include <net/pfvar.h>
#include <net/pfvar_priv.h>

extern int pf_test(sa_family_t, int, struct pfk_ifnet *, struct mbuf **);
extern void divert_packet(struct mbuf *, int, u_int16_t);
extern struct pf_divert *pf_find_divert(struct mbuf *);
extern struct workqueue_struct *pfk_netisr_wq;

/* PFLINUX: netisr work item — carries ownership of one sk_buff */
struct pfk_pkt {
	struct work_struct	work;
	struct sk_buff		*skb;
	struct pfk_ifnet	*ifp;
	sa_family_t		af;
	int			dir;
};

#define PFK_DONE_IN	0x00000001u	/* skb->mark bit: PF_IN pass completed */
#define PFK_DONE_OUT	0x00000002u	/* skb->mark bit: PF_OUT pass completed */

static void
pfk_pkt_reinject(struct pfk_pkt *pk)
{
	struct sk_buff *skb = pk->skb;

	pk->skb->mark |= (pk->dir == PF_IN) ? PFK_DONE_IN : PFK_DONE_OUT;
	if (pk->dir == PF_IN) {
		/* netif_rx expects data at the network header (eth_type_trans
		 * already done by the driver) — exactly the state the PRE
		 * hook sees.  protocol/mac_header are preserved. */
		if (skb->dev) {
			skb_reset_network_header(skb);
			skb->protocol = htons(pk->af == AF_INET ?
				ETH_P_IP : ETH_P_IPV6);
			netif_rx(skb);
		} else {
			kfree_skb(skb);
		}
	} else {
		/* continue the output path: dst is attached at both
		 * LOCAL_OUT and POST_ROUTING time */
		struct dst_entry *d = skb_dst(skb);

		if (d && d->dev)
			skb->dev = d->dev;
		if (d && !IS_ERR(d) && d->output)
			d->output(&init_net, skb->sk, skb);
		else
			kfree_skb(skb);
	}
	kfree(pk);
}


/*
 * Reverse NAT: pf rewrote the destination of an inbound packet (the reply
 * of a translated flow).  OpenBSD re-routes such a packet in ip_input()
 * (PF_TAG_REROUTE); Linux has no such layer, so re-route it here and push
 * it out through the new egress device.
 */
static void
pfk_reroute(struct sk_buff *skb)
{
	struct flowi4	 fl4 = { };
	struct rtable	*rt;
	struct iphdr	*iph = ip_hdr(skb);

	fl4.daddr = iph->daddr;
	/*
	 * Leave the source empty: the packet keeps the original source
	 * (the peer's address), but the kernel would reject a route lookup
	 * with a non-local source (ENETUNREACH).
	 */
	fl4.saddr = 0;
	fl4.flowi4_oif = 0;
	fl4.flowi4_mark = 0;
	fl4.flowi4_scope = RT_SCOPE_UNIVERSE;

	rt = ip_route_output_key(&init_net, &fl4);
	if (IS_ERR(rt) || rt == NULL) {
		kfree_skb(skb);
		return;
	}
	skb_dst_set(skb, &rt->dst);
	skb->dev = rt->dst.dev;
	skb_reset_network_header(skb);
	rt->dst.output(&init_net, skb->sk, skb);
}

/*
 * PFLINUX: pf rewrote the IPv4 addresses of the packet (NAT).  OpenBSD's
 * ip_output() recomputes the IP header checksum on every packet it sends;
 * Linux does not, so fix it up here after pf touched the header.
 */
/*
 * pf rewrote the addresses of this packet.  OpenBSD recomputes the IP header
 * checksum in ip_output() and the transport checksum is patched incrementally
 * by pf - but that only works on a *materialized* checksum, and VM-to-VM
 * traffic routinely arrives with the transport checksum still uncomputed
 * (the sender's offload never completed on the tap).  So recompute from
 * scratch here:
 *
 *   - GSO segments: hand the job back to the NIC (CHECKSUM_PARTIAL with the
 *     transport offset); computing in software would only cover the first
 *     segment and silently corrupt the rest (this is what kills bulk TCP).
 *   - everything else: compute the transport checksum over the new
 *     pseudo-header in software, then the IP header checksum.
 */
static void
pfk_fix_csum_after_translate(struct sk_buff *skb)
{
	struct iphdr *iph;
	unsigned int hlen, len;
	__sum16 *csum;
	__u8 proto;

	if (!pskb_may_pull(skb, sizeof(struct iphdr)))
		return;
	iph = ip_hdr(skb);
	hlen = iph->ihl * 4;
	proto = iph->protocol;
	len = skb->len - hlen;

	if (proto == IPPROTO_TCP || proto == IPPROTO_UDP) {
		if (!pskb_may_pull(skb, hlen + (proto == IPPROTO_TCP ?
		    sizeof(struct tcphdr) : sizeof(struct udphdr))))
			return;
		csum = (__sum16 *)(skb->data + hlen +
		    (proto == IPPROTO_TCP ? offsetof(struct tcphdr, check)
					 : offsetof(struct udphdr, check)));
		if (skb_is_gso(skb)) {
			skb->csum_start = (unsigned short)
			    (((unsigned char *)skb->data + hlen) - skb->head);
			skb->csum_offset = proto == IPPROTO_TCP ?
			    offsetof(struct tcphdr, check) :
			    offsetof(struct udphdr, check);
			skb->ip_summed = CHECKSUM_PARTIAL;
		} else {
			*csum = 0;
			*csum = csum_tcpudp_magic(iph->saddr, iph->daddr, len,
			    proto, skb_checksum(skb, hlen, len, 0));
			skb->ip_summed = CHECKSUM_NONE;
		}
	}

	iph = ip_hdr(skb);	/* pskb_may_pull may have moved the data */
	iph->check = 0;
	iph->check = csum_fold(csum_partial((__u8 *)iph, iph->ihl * 4, 0));
}

static void
pfk_pkt_work(struct work_struct *w)
{
	struct pfk_pkt *pk = container_of(w, struct pfk_pkt, work);
	struct mbuf *m, *m0;
	struct sk_buff *orig;
	__be32 orig_dst = 0, orig_src = 0;
	int action;

	if (pk->af == AF_INET && pskb_may_pull(pk->skb, sizeof(struct iphdr))) {
		orig_dst = ip_hdr(pk->skb)->daddr;
		orig_src = ip_hdr(pk->skb)->saddr;
	}

	m = pfk_m_wrap_skb(pk->skb, pk->af);
	if (!m) {
		pfk_pkt_reinject(pk);	/* like "pf not consulted" */
		return;
	}
	m0 = m;

	/* PFLINUX: no NET read lock on the data path (M2 interim).  The
	 * rw-semaphore read side starves softirq-fed packet flows whenever a
	 * control writer parks (purge/ioctl), and BSD's NET_LOCK semantics
	 * are being replaced by pf_lock/RCU work in M5+.  Rule-set mutation
	 * and packet walk run concurrently from here on — documented. */
	action = pf_test(pk->af, pk->dir, pk->ifp, &m);

	if (pk->af == AF_INET && orig_src != 0 &&
	    pskb_may_pull(pk->skb, sizeof(struct iphdr)) &&
	    (ip_hdr(pk->skb)->saddr != orig_src ||
	    ip_hdr(pk->skb)->daddr != orig_dst))
		pfk_fix_csum_after_translate(pk->skb);


	if (m == m0) {
		/* same buffer, possibly rewritten in place: our wrapper
		 * held one extra skb reference — release it */
		m_freem(m);
		if (action == PF_PASS && orig_dst != 0 &&
		    pk->af == AF_INET && pk->dir == PF_IN &&
		    pskb_may_pull(pk->skb, sizeof(struct iphdr)) &&
		    ip_hdr(pk->skb)->daddr != orig_dst) {
			/* pf translated the destination (reverse NAT):
			 * re-route instead of delivering locally */
			pfk_reroute(pk->skb);
			kfree(pk);
			return;
		}
		if (action == PF_PASS) {
			pfk_pkt_reinject(pk);
			return;
		}
		kfree_skb(pk->skb);	/* our queue reference */
		kfree(pk);
		return;
	}

	/* m != m0: pf consumed the wrapper (and its extra reference).
	 * Our queue reference on the original must be resolved here. */
	orig = pk->skb;
	if (m == NULL) {
		/* buffered (fragment queue) or consumed by divert/forward:
		 * pf kept its own references; drop our queue reference */
		kfree_skb(orig);
		kfree(pk);
		return;
	}
	/* replacement (reassembly survivor): free our original ref, then
	 * inject the survivor through the stack */
	pk->skb = m_to_skb(m);
	m->skb = NULL;
	m_freem(m);		/* wrapper bookkeeping only */
	kfree_skb(orig);
	pfk_pkt_reinject(pk);
}

/* shared: decide whether/how to queue this packet */
static unsigned int
pfk_enqueue_verdict(int af, struct sk_buff *skb,
	const struct nf_hook_state *st)
{
	struct pfk_pkt *pk;
	struct pfk_ifnet *ifp;
	unsigned int pk_dir_done __maybe_unused;
	int dir;

	switch (st->hook) {
	case NF_INET_PRE_ROUTING:
		/* OpenBSD evaluates the IN ruleset in ip_input(); every
		 * arriving packet passes it. */
		dir = PF_IN;
		break;
	case NF_INET_FORWARD:
		/* OpenBSD evaluates the OUT ruleset of a forwarded packet in
		 * ip_forward() (pf_test(PF_FWD)); the IN ruleset was already
		 * applied at input time.  Without this, transit traffic never
		 * sees the "out" rules and state matching for replies is
		 * impossible. */
		dir = PF_OUT;
		break;
	case NF_INET_LOCAL_OUT:
	case NF_INET_POST_ROUTING:
		dir = PF_OUT;
		break;
	default:
		return NF_ACCEPT;	/* LOCAL_IN */
	}
	if (skb->mark & PFK_SKIP_PF)
		return NF_ACCEPT;	/* reinjected by divert: don't re-filter */
	if (skb->mark & (dir == PF_IN ? PFK_DONE_IN : PFK_DONE_OUT))
		return NF_ACCEPT;

	if (af == AF_INET && !pskb_may_pull(skb, sizeof(struct pf_ip)))
		return NF_ACCEPT;
	if (af == AF_INET6 && !pskb_may_pull(skb, sizeof(struct pf_ip6_hdr)))
		return NF_ACCEPT;

	if (dir == PF_IN)
		ifp = pfk_ifp_bydev(st->in ? st->in : skb->dev);
	else
		ifp = pfk_ifp_bydev(st->out ? st->out :
		    (skb->dev ? skb->dev : NULL));
	if (!ifp)
		return NF_ACCEPT;

	pk = kmalloc(sizeof(*pk), GFP_ATOMIC);
	if (!pk)
		return NF_ACCEPT;	/* overload guard: bypass like full pfq */
	INIT_WORK(&pk->work, pfk_pkt_work);
	skb_get(skb);			/* our queue reference */
	pk->skb = skb;
	pk->ifp = ifp;
	pk->af = af;
	pk->dir = dir;
	queue_work(pfk_netisr_wq, &pk->work);
	return NF_STOLEN;
}

struct workqueue_struct *pfk_netisr_wq;

static unsigned int
pfk_ipv4_hook(void *priv, struct sk_buff *skb, const struct nf_hook_state *st)
{
	return pfk_enqueue_verdict(AF_INET, skb, st);
}

static unsigned int
pfk_ipv6_hook(void *priv, struct sk_buff *skb, const struct nf_hook_state *st)
{
	return pfk_enqueue_verdict(AF_INET6, skb, st);
}

static struct nf_hook_ops pfk_hooks[] = {
	{ .hook = pfk_ipv4_hook, .pf = NFPROTO_IPV4, .hooknum = NF_INET_PRE_ROUTING,
	  .priority = NF_IP_PRI_FIRST },
	{ .hook = pfk_ipv4_hook, .pf = NFPROTO_IPV4, .hooknum = NF_INET_LOCAL_IN,
	  .priority = NF_IP_PRI_FIRST + 1 },
	{ .hook = pfk_ipv4_hook, .pf = NFPROTO_IPV4, .hooknum = NF_INET_FORWARD,
	  .priority = NF_IP_PRI_FIRST },
	{ .hook = pfk_ipv4_hook, .pf = NFPROTO_IPV4, .hooknum = NF_INET_LOCAL_OUT,
	  .priority = NF_IP_PRI_CONNTRACK + 1 },
	{ .hook = pfk_ipv4_hook, .pf = NFPROTO_IPV4, .hooknum = NF_INET_POST_ROUTING,
	  .priority = NF_IP_PRI_LAST - 1 },
	{ .hook = pfk_ipv6_hook, .pf = NFPROTO_IPV6, .hooknum = NF_INET_PRE_ROUTING,
	  .priority = NF_IP_PRI_FIRST },
	{ .hook = pfk_ipv6_hook, .pf = NFPROTO_IPV6, .hooknum = NF_INET_LOCAL_IN,
	  .priority = NF_IP_PRI_FIRST + 1 },
	{ .hook = pfk_ipv6_hook, .pf = NFPROTO_IPV6, .hooknum = NF_INET_FORWARD,
	  .priority = NF_IP_PRI_FIRST },
	{ .hook = pfk_ipv6_hook, .pf = NFPROTO_IPV6, .hooknum = NF_INET_LOCAL_OUT,
	  .priority = NF_IP_PRI_CONNTRACK + 1 },
	{ .hook = pfk_ipv6_hook, .pf = NFPROTO_IPV6, .hooknum = NF_INET_POST_ROUTING,
	  .priority = NF_IP_PRI_LAST - 1 },
};
#define PFK_NHOOKS ARRAY_SIZE(pfk_hooks)

int
pfk_hooks_register(void)
{
	int i, err;

	pfk_netisr_wq = alloc_ordered_workqueue("pfk_netisr", WQ_MEM_RECLAIM);
	if (!pfk_netisr_wq)
		return -ENOMEM;
	for (i = 0; i < PFK_NHOOKS; i++) {
		pfk_hooks[i].priv = NULL;
		err = nf_register_net_hook(&init_net, &pfk_hooks[i]);
		if (err)
			goto fail;
	}
	pr_info("pfk: netfilter hooks registered (%zu, netisr wq data path)\n",
		(size_t)PFK_NHOOKS);
	return 0;
fail:
	while (--i >= 0)
		nf_unregister_net_hook(&init_net, &pfk_hooks[i]);
	destroy_workqueue(pfk_netisr_wq);
	pfk_netisr_wq = NULL;
	return err;
}

void
pfk_hooks_unregister(void)
{
	int i;

	for (i = 0; i < PFK_NHOOKS; i++)
		nf_unregister_net_hook(&init_net, &pfk_hooks[i]);
	if (pfk_netisr_wq) {
		flush_workqueue(pfk_netisr_wq);
		destroy_workqueue(pfk_netisr_wq);
		pfk_netisr_wq = NULL;
	}
}
