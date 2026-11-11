/*
 * PF-on-Linux netfilter hook registration + M1 ioctl skeleton.
 * The real verdict logic arrives from staged pf.c (T12); this file owns
 * hook lifecycle and /dev/pf dispatch. (ISC 2026)
 */
#include <pfk.h>
#include <pfk_api.h>
#include <net/pfvar.h>
#include <net/pfvar_priv.h>

static int pfk_enabled = 1;	/* like pf_defaultwise/pf_enable on boot */
static struct pf_status pfk_stat;

/* --- temporary M1 control surface (replaced by staged pf_ioctl.c, T7b) --- */
static int pfk_ioctl_min(struct file *f, unsigned int cmd, unsigned long addr)
{
	struct pf_status s;

	switch (cmd) {
	case DIOCGETSTATUS:
		memset(&s, 0, sizeof(s));
		s.running = pfk_enabled;
		strscpy(s.ifname, "pf", sizeof(s.ifname));
		if (copy_to_user((void __user *)addr, &s, sizeof(s)))
			return EFAULT;
		return 0;
	default:
		/* Rule/state/table ioctls arrive with the staged pf_ioctl.c
		 * (plan T7b); until then they are rejected (not silently
		 * accepted) so behaviour is explicit. */
		return EINVAL;
	}
}

long pfk_ioctl(struct file *f, unsigned int cmd, unsigned long addr)
{
	return pfk_ioctl_min(f, cmd, addr);
}

void pfk_status(struct pf_status *s)
{
	if (s)
		*s = pfk_stat;
}
int pfk_enable(int on) { pfk_enabled = on ? 1 : 0; return 0; }

/* --- netfilter hooks: ingress/egress plumbing (real work lands in T12) --- */
static unsigned int
pfk_ipv4_hook(void *priv, struct sk_buff *skb, const struct nf_hook_state *st)
{
	/* PFLINUX M1: accept-everything placeholder; T12 routes to pf_test() */
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

int pfk_hooks_register(void)
{
	int i, err;

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

void pfk_hooks_unregister(void)
{
	int i;

	for (i = 0; i < PFK_NHOOKS; i++)
		nf_unregister_net_hook(&init_net, &pfk_hooks[i]);
}
