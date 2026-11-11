/*
 * PF-on-Linux netfilter hook registration + M2 data path.
 *
 * One pf_test() pass per BSD ip-stack call site:
 *   NF_INET_PRE_ROUTING  → pf_test(PF_IN)   (OpenBSD ip_input)
 *   NF_INET_LOCAL_OUT    → pf_test(PF_OUT)  (OpenBSD ip_output, early)
 *   NF_INET_POST_ROUTING → pf_test(PF_OUT)  (OpenBSD ip_output, routed)
 * LOCAL_IN / FORWARD are NF_ACCEPT: the packet was already evaluated on
 * its PRE (input) and POST (output) passes, exactly like OpenBSD which
 * runs PF once per direction inside ip_input/ip_output.
 *
 * Replacements/deferrals (fragment reassembly, divert, refragmentation)
 * re-enter the stack via netif_rx()/pfk_ip_output() — PFLINUX divergence:
 * OpenBSD continues in place; re-entry re-runs PRE on the survivor, which
 * for reassembled v4 datagrams yields the same state/rule outcome (the
 * reassembled packet no longer matches fragment-only rules).  Documented
 * in docs/porting-notes.md.
 *
 * (c) 2026 PF-on-Linux contributors — ISC
 */
#include <pfk.h>
#include <pfk_api.h>
#include <net/pfvar.h>
#include <net/pfvar_priv.h>

extern int pf_test(sa_family_t, int, struct pfk_ifnet *, struct mbuf **);

/* shared verdict plumbing for v4/v6 (af decides header pull size) */
static unsigned int
pfk_run(int af, struct sk_buff *skb, const struct nf_hook_state *st)
{
	struct pfk_ifnet *ifp;
	struct mbuf *m, *m0;
	int dir, action;

	if (af == AF_INET &&
	    !pskb_may_pull(skb, sizeof(struct pf_ip)))
		return NF_ACCEPT;
	if (af == AF_INET6 &&
	    !pskb_may_pull(skb, sizeof(struct pf_ip6_hdr)))
		return NF_ACCEPT;

	switch (st->hook) {
	case NF_INET_PRE_ROUTING:
		dir = PF_IN;
		break;
	case NF_INET_LOCAL_OUT:
	case NF_INET_POST_ROUTING:
		dir = PF_OUT;
		break;
	default:
		return NF_ACCEPT;	/* LOCAL_IN/FORWARD: seen already */
	}

	/* ifp: OpenBSD passes the *input* interface on PF_IN and the
	 * output interface on PF_OUT (from the route). */
	if (dir == PF_IN)
		ifp = pfk_ifp_bydev(st->in ? st->in : skb->dev);
	else
		ifp = pfk_ifp_bydev(skb->dev ? skb->dev : st->out);
	if (!ifp)
		return NF_ACCEPT;	/* unknown iface: inert, like no pf */

	m = pfk_m_wrap_skb(skb, af);
	if (!m)
		return NF_ACCEPT;
	m0 = m;

	pfk_net_read_enter();
	action = pf_test(af, dir, ifp, &m);
	pfk_net_read_release();

	if (m == m0) {
		/* in-place (possible header rewrite already reflected in
		 * the shared skb); our extra ref is released by m_freem */
		m_freem(m);
		return (action == PF_PASS) ? NF_ACCEPT : NF_DROP;
	}

	/* m != m0: either NULL (consumed: buffered fragment, divert,
	 * refragment-then-free, drop-after-replacement) or a *new* mbuf
	 * (reassembly survivor). m0 itself was consumed by pf. */
	if (m == NULL) {
		kfree_skb(skb);		/* we own the hook ref now */
		return NF_STOLEN;
	}
	/* survivor: drop the netfilter original, re-inject the survivor */
	kfree_skb(skb);
	if (dir == PF_IN) {
		struct sk_buff *ns = m_to_skb(m);

		ns->protocol = htons(af == AF_INET ? ETH_P_IP : ETH_P_IPV6);
		if (!ns->dev)
			ns->dev = st->in;
		if (ns->dev)
			dev_hold(ns->dev);
		else {
			kfree_skb(ns);
			m->skb = NULL;
			m_freem(m);
			return NF_STOLEN;
		}
		netif_rx(ns);
		m->skb = NULL;	/* reference moved into netif_rx */
		m_freem(m);
	} else {
		/* survivor leaves via the BSD output helper (consumes m) */
		pfk_ip_output(m, NULL, NULL, 0, NULL, NULL, 0);
	}
	return NF_STOLEN;
}

static unsigned int
pfk_ipv4_hook(void *priv, struct sk_buff *skb,
	const struct nf_hook_state *st)
{
	return pfk_run(AF_INET, skb, st);
}

static unsigned int
pfk_ipv6_hook(void *priv, struct sk_buff *skb,
	const struct nf_hook_state *st)
{
	return pfk_run(AF_INET6, skb, st);
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

	pr_info("pfk: netfilter hooks registering (%zu, verdict path live)\n",
		(size_t)PFK_NHOOKS);
	for (i = 0; i < PFK_NHOOKS; i++) {
		pfk_hooks[i].priv = NULL;
		err = nf_register_net_hook(&init_net, &pfk_hooks[i]);
		if (err)
			goto fail;
	}
	return 0;
fail:
	while (--i >= 0)
		nf_unregister_net_hook(&init_net, &pfk_hooks[i]);
	return err;
}

void
pfk_hooks_unregister(void)
{
	int i;

	for (i = 0; i < PFK_NHOOKS; i++)
		nf_unregister_net_hook(&init_net, &pfk_hooks[i]);
}
