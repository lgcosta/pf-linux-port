/*
 * PF-on-Linux netfilter hook registration + engine status surface.
 * The verdict path is staged pf.c (pf_test()); hook→pf_test wiring with
 * mbuf ingress is milestone M2's data-path task — until then the hooks
 * accept so the module is inert once loaded (never silent: pr_info at
 * registration says so). (ISC 2026)
 */
#include <pfk.h>
#include <pfk_api.h>
#include <net/pfvar.h>
#include <net/pfvar_priv.h>

static int pfk_enabled = 1;	/* like pf_defaultwise/pf_enable on boot */
static struct pf_status pfk_stat;

void pfk_status(struct pf_status *s)
{
	if (s)
		*s = pfk_stat;
}

int pfk_enable(int on)
{
	pfk_enabled = on ? 1 : 0;
	return 0;
}

/* --- netfilter hooks: ingress/egress plumbing --- */
static unsigned int
pfk_ipv4_hook(void *priv, struct sk_buff *skb, const struct nf_hook_state *st)
{
	/* PFLINUX M1: pf_test() routing arrives with the M2 data-path task;
	 * until then the hooks accept (module inert). */
	return NF_ACCEPT;
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
};
#define PFK_NHOOKS ARRAY_SIZE(pfk_hooks)

int
pfk_hooks_register(void)
{
	int i, err;

	pr_info("pfk: netfilter hooks registered (verdict path inert until M2)\n");
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
