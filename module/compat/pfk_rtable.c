/*
 * PFLINUX rtable/route compatibility (ISC 2026).
 * Linux has no per-rtableid FIBs: rtable_exists() accepts every id and
 * rtable_iterate() stops after the first route (multipath iteration is
 * not modelled; recorded deviation).  The PF route-to/mb-path checks get
 * their answers from the Linux FIB here; forwarding itself happens in
 * pfk_ip4.c/pfk_xmit.c.
 *
 * struct pf_rtentry instances are small heap snapshots of the Linux
 * route: egress ifindex + a BSD-shaped interface address view (needed by
 * ifatoia/ifatoia6 in pf.c) + label id (always 0, labels not modelled).
 */
#include <pfk.h>
#include "compat/net/rtable.h"

int
rtable_exists(unsigned int rtableid)
{
	/* PFLINUX: single global FIB → every requested id exists. */
	return 1;
}

struct pf_rtentry *
rtable_iterate(struct pf_rtentry *rt)
{
	/* PFLINUX: multipath route iteration is not modelled; PF falls
	 * back to the single Linux FIB answer (checked once). */
	(void)rt;
	return NULL;
}

/* attach a BSD interface-address view for the egress device */
static void
pfk_rt_fill_ifa(struct pf_rtentry *rt, struct pfk_ifnet *ifp, int family)
{
	struct pfk_in_ifaddr *ia4;
	struct pfk_in6_ifaddr *ia6;
	struct ifaddr *ifa;

	if (!ifp)
		return;
	if (family == AF_INET) {
		ia4 = pfk_malloc(sizeof(*ia4), M_NOWAIT | M_ZERO);
		if (!ia4)
			return;
		TAILQ_FOREACH(ifa, &ifp->if_addrlist, ifa_list) {
			if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
				ia4->ia_addr = *sintosa(ifa->ifa_addr);
				break;
			}
		}
		rt->rt_ifa = ia4;
	} else {
		ia6 = pfk_malloc(sizeof(*ia6), M_NOWAIT | M_ZERO);
		if (!ia6)
			return;
		TAILQ_FOREACH(ifa, &ifp->if_addrlist, ifa_list) {
			if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET6) {
				ia6->ia_addr = *sin6tosa(ifa->ifa_addr);
				break;
			}
		}
		rt->rt_ifa = ia6;
	}
}

static struct pf_rtentry *
pfk_rtentry_from_dst(struct dst_entry *d, const struct sockaddr *dst)
{
	struct pf_rtentry *rt;
	struct pfk_ifnet *ifp;

	rt = pfk_malloc(sizeof(*rt), M_NOWAIT | M_ZERO);
	if (!rt)
		return NULL;
	rt->rt_addr = (void *)dst;	/* borrowed; not freed */
	rt->rt_ifidx = d->dev ? d->dev->ifindex : 0;
	rt->rt_flags = RTF_UP;
	ifp = d->dev ? pfk_ifp_byindex(d->dev->ifindex) : NULL;
	pfk_rt_fill_ifa(rt, ifp, dst->sa_family);
	return rt;
}

struct pf_rtentry *
rtalloc(const void *sap, int flags, unsigned int rtableid)
{
	const struct sockaddr *sa = (const struct sockaddr *)sap;
	struct pf_rtentry *rt = NULL;

	(void)flags; (void)rtableid;	/* PFLINUX: one FIB, resolve implied */
	switch (sa->sa_family) {
	case AF_INET: {
		struct flowi4 fl4 = { };
		struct rtable *r4;

		fl4.daddr = sintosa(sa)->sin_addr.s_addr;
		r4 = ip_route_output_key(&init_net, &fl4);
		if (!IS_ERR(r4)) {
			rt = pfk_rtentry_from_dst(&r4->dst, sa);
			dst_release(&r4->dst);
		}
		break;
	}
	case AF_INET6: {
#if IS_ENABLED(CONFIG_IPV6)
		struct flowi6 fl6 = { };
		struct dst_entry *d;

		fl6.daddr = sin6tosa(sa)->sin6_addr;
		d = ip6_route_output(&init_net, NULL, &fl6);
		if (d && !d->error) {
			rt = pfk_rtentry_from_dst(d, sa);
			dst_release(d);
		} else if (d)
			dst_release(d);
		break;
#endif
	}
	default:
		break;
	}
	return rt;
}

struct pf_rtentry *
rtalloc_mpath(const void *sa, uint32_t *hash, unsigned int rtableid)
{
	/* PFLINUX: no multipath selection; hash hint unused. */
	(void)hash;
	return rtalloc(sa, RT_RESOLVE, rtableid);
}

int
rtisvalid(struct pf_rtentry *rt)
{
	return rt != NULL;
}

void
rtfree(struct pf_rtentry *rt)
{
	if (!rt)
		return;
	if (rt->rt_ifa)
		pfk_free(rt->rt_ifa);
	pfk_free(rt);
}

/* ---------------- route labels ---------------- */
/* Routing domains/labels are a non-goal of the port; these satisfy the
 * ioctl layer and report "unavailable" instead of pretending (pfk.h). */
int
rtlabel_name2id(const char *name)
{
	(void)name;
	return (0);		/* 0 == no label, pf_ioctl treats as failure */
}

char *
rtlabel_id2name(unsigned int id, char *buf, size_t len)
{
	(void)id; (void)buf; (void)len;
	return (NULL);
}

void
rtlabel_unref(unsigned int id)
{
	(void)id;
}
