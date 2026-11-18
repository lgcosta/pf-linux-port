/*
 * PF-on-Linux ifnet/address registry (ISC 2026).
 * Maintains BSD-flavored struct pfk_ifnet views over net_device, refreshed
 * address snapshots, egress tracking, and name/index lookups. All ops under
 * NET_LOCK (read for lookups, write for list mutation) except notifier which
 * uses its own spinlock + work.
 */
#include <pfk.h>

static void pfk_if_event_call(int added, struct pfk_ifnet *ifp);

static DEFINE_IDR(pfk_ifnet_idr);
static DEFINE_SPINLOCK(pfk_ifnet_lock);
static struct pfk_ifnet *pfk_egress;

static void pfk_ifnet_release(struct rcu_head *rhp)
{
	struct pfk_ifnet *ifp = container_of(rhp, struct pfk_ifnet, rcu);

	pfk_if_free(ifp);
}

static struct pfk_ifnet *
pfk_ifnet_new(struct net_device *dev)
{
	struct pfk_ifnet *ifp = kzalloc(sizeof(*ifp), GFP_KERNEL);

	if (!ifp)
		return NULL;
	strscpy(ifp->if_xname, dev->name, sizeof(ifp->if_xname));
	ifp->if_index = dev->ifindex;
	ifp->dev = dev;
	ifp->alive = 1;
	INIT_LIST_HEAD(&ifp->link);
	/* BSD TAILQ heads are self-referential when empty (tqh_last points
	 * at &tqh_first); kzalloc leaves tqh_last == NULL, which the first
	 * TAILQ_INSERT_TAIL would dereference. */
	TAILQ_INIT(&ifp->if_addrlist);
	TAILQ_INIT(&ifp->if_groups);
	ifp->if_type = IFT_OTHER;
	switch (dev->type) {
	case ARPHRD_ETHER: ifp->if_type = IFT_ETHER; break;
	case ARPHRD_LOOPBACK: ifp->if_type = IFT_LOOP; break;
	case ARPHRD_PPP: ifp->if_type = IFT_PPPOINT; break;
	case ARPHRD_SIT: case ARPHRD_TUNNEL: case ARPHRD_IP6GRE:
		ifp->if_type = IFT_GIF; break;
	}
	return ifp;
}

static int
pfk_dev_flags_bsd(struct net_device *dev)
{
	int f = 0;

	if (dev->flags & IFF_UP)		f |= PFIFF_UP;
	if (dev->flags & IFF_BROADCAST)		f |= PFIFF_BROADCAST;
	if (dev->flags & IFF_DEBUG)		f |= PFIFF_DEBUG;
	if (dev->flags & IFF_LOOPBACK)		f |= PFIFF_LOOPBACK;
	if (dev->flags & IFF_POINTOPOINT)	f |= PFIFF_POINTOPOINT;
	if (netif_running(dev))			f |= PFIFF_RUNNING;
	if (dev->flags & IFF_PROMISC)		f |= PFIFF_PROMISC;
	if (dev->flags & IFF_ALLMULTI)		f |= PFIFF_ALLMULTI;
	if (dev->flags & IFF_MULTICAST)		f |= PFIFF_MULTICAST;
	if (!netif_running(dev))	f |= PFIFF_RESOLVE;
	return f;
}

void
pfk_if_refresh(struct pfk_ifnet *ifp)
{
	ifp->if_mtu = ifp->dev->mtu;
	ifp->if_flags = pfk_dev_flags_bsd(ifp->dev);
	if (ifp->dev->hard_header_len > max_linkhdr)
		max_linkhdr = ifp->dev->hard_header_len;
	/* PFLINUX: sync the BSD sysctl mirrors on device events */
	pfk_sysctl_sync();
	/* address snapshot: rebuild list from Linux in_ifaddr/inet6_ifaddr */
	pfk_if_addrlist_refresh(ifp);
}

/*
 * OpenBSD if_get/if_put: registry entries are refcounted there; the port's
 * pfk_ifnet objects are freed via call_rcu on UNREGISTER, so RCU/NET_LOCK
 * readers can use them without an extra count (PFLINUX: if_put is a no-op).
 */
struct pfk_ifnet *
if_get(int ifidx)
{
	return pfk_ifp_byindex(ifidx);
}

struct pfk_ifnet *
if_get_smr(int ifidx)
{
	return pfk_ifp_byindex(ifidx);
}

void
if_put(struct pfk_ifnet *ifp)
{
	(void)ifp;
}

struct pfk_ifnet *
pfk_ifp_byindex(int idx)
{
	struct pfk_ifnet *ifp;

	if (idx <= 0)
		return NULL;
	ifp = idr_find(&pfk_ifnet_idr, idx);
	return ifp && ifp->alive ? ifp : NULL;
}

struct pfk_ifnet *
pfk_ifp_bydev(struct net_device *dev)
{
	return dev ? pfk_ifp_byindex(dev->ifindex) : NULL;
}

struct pfk_ifnet *
pfk_ifp_byname(const char *name)
{
	struct net_device *dev;
	struct pfk_ifnet *ifp;

	dev = dev_get_by_name(&init_net, name);
	if (!dev)
		return NULL;
	ifp = pfk_ifp_bydev(dev);
	dev_put(dev);
	return ifp;
}

void
pfk_if_stats(struct pfk_ifnet *ifp, struct pfk_if_data *out)
{
	memset(out, 0, sizeof(*out));
	out->ifi_ibytes = ifp->dev->stats.rx_bytes;
	out->ifi_obytes = ifp->dev->stats.tx_bytes;
}

/* -------- address snapshots -------- */
/* Build a BSD-style ifaddr for the given interface address. */
static struct ifaddr *
pfkifa_new(struct pfk_ifnet *ifp, int family)
{
	struct ifaddr *ifa = kzalloc(sizeof(*ifa), GFP_KERNEL);

	if (!ifa)
		return NULL;
	ifa->ifa_ifp = ifp;
	refcnt_init(&ifa->ifa_refcnt);
	return ifa;
}

/*
 * PFLINUX: a cheap fingerprint of an interface's current IP addresses.
 *
 * Linux does not export an IP-address notifier to modules: the IPv4
 * address chain (inetaddr_chain) and its IPv6 counterpart are private to
 * the network core, and NETDEV_CHANGEADDR only covers MAC address changes.
 * The only in-module way to notice that an address landed (or moved) is to
 * look, so the compat layer polls this fingerprint once a second and
 * notifies the PF engine when it changes.  The fingerprint is a hash of the
 * address count and the addresses themselves - no allocation, RCU read only.
 */
static u32
pfk_if_addr_fingerprint(struct net_device *dev)
{
	u32 fp = 0x811c9dc5;		/* FNV-1a */
	u32 n = 0;
	struct in_device *idev;
	struct inet6_dev *idev6;
	struct in_ifaddr *ifa4;
	struct inet6_ifaddr *ifa6;

	/* PFLINUX: __in_dev_get_rcu() requires the RCU read side - the
	 * address lists are freed with an RCU grace period. */
	rcu_read_lock();
	idev = __in_dev_get_rcu(dev);
	if (idev) {
		for (ifa4 = rcu_dereference(idev->ifa_list); ifa4;
		     ifa4 = rcu_dereference(ifa4->ifa_next)) {
			fp = (fp ^ ifa4->ifa_address) * 16777619u;
			n++;
		}
	}
#if IS_ENABLED(CONFIG_IPV6)
	idev6 = __in6_dev_get(dev);
	if (idev6) {
		list_for_each_entry(ifa6, &idev6->addr_list, if_list) {
			int i;

			for (i = 0; i < 16; i++)
				fp = (fp ^ ifa6->addr.s6_addr[i]) * 16777619u;
			n++;
		}
	}
#endif
	rcu_read_unlock();

	return ((fp ^ n) * 16777619u);
}

int
pfk_if_addrlist_refresh(struct pfk_ifnet *ifp)
{
	struct ifaddr *ifa;
	struct net_device *dev = ifp->dev;

	/* free old */
	while (!TAILQ_EMPTY(&ifp->if_addrlist)) {
		ifa = TAILQ_FIRST(&ifp->if_addrlist);
		TAILQ_REMOVE(&ifp->if_addrlist, ifa, ifa_list);
		kfree(ifa->ifa_addr);
		kfree(ifa);
	}

	rcu_read_lock();
	/* IPv4 */
	{
		struct in_device *idev = __in_dev_get_rcu(dev);
		struct in_ifaddr *ifa4;

		if (idev)
			for (ifa4 = rcu_dereference(idev->ifa_list); ifa4;
			     ifa4 = rcu_dereference(ifa4->ifa_next)) {
				ifa = pfkifa_new(ifp, AF_INET);
				if (!ifa)
					continue;
				ifa->ifa_addr = kmalloc(sizeof(struct sockaddr_in),
				    GFP_KERNEL);
				if (!ifa->ifa_addr) {
					kfree(ifa);
					continue;
				}
				{
					struct sockaddr_in *sin =
						sintosa(ifa->ifa_addr);
					memset(sin, 0, sizeof(*sin));
					sin->sin_family = AF_INET;
					sin->sin_addr.s_addr = ifa4->ifa_address;
				}
				TAILQ_INSERT_TAIL(&ifp->if_addrlist, ifa, ifa_list);
			}
	}
	/* IPv6 */
#if IS_ENABLED(CONFIG_IPV6)
	{
		struct inet6_dev *idev6 = __in6_dev_get(dev);
		struct inet6_ifaddr *ifa6;

		if (idev6)
			list_for_each_entry(ifa6, &idev6->addr_list, if_list) {
				ifa = pfkifa_new(ifp, AF_INET6);
				if (!ifa)
					continue;
				ifa->ifa_addr = kmalloc(sizeof(struct sockaddr_in6),
				    GFP_KERNEL);
				if (!ifa->ifa_addr) {
					kfree(ifa);
					continue;
				}
				{
					struct sockaddr_in6 *sin6 =
						sin6tosa(ifa->ifa_addr);
					memset(sin6, 0, sizeof(*sin6));
					sin6->sin6_family = AF_INET6;
					sin6->sin6_addr = ifa6->addr;
				}
				TAILQ_INSERT_TAIL(&ifp->if_addrlist, ifa, ifa_list);
			}
	}
#endif
	rcu_read_unlock();

	/* remember what we just published, so the poll can tell a change */
	ifp->if_addr_fp = pfk_if_addr_fingerprint(dev);
	return 0;
}

struct ifaddr *
pfk_ifwithaddr(const struct sockaddr *sa)
{
	struct pfk_ifnet *ifp;
	struct ifaddr *ifa;

	for (ifp = pfk_if_first(); ifp; ifp = pfk_if_next(ifp)) {
		TAILQ_FOREACH(ifa, &ifp->if_addrlist, ifa_list) {
			if (!ifa->ifa_addr ||
			    ifa->ifa_addr->sa_family != sa->sa_family)
				continue;
			if (sa->sa_family == AF_INET &&
			    sintosa(ifa->ifa_addr)->sin_addr.s_addr ==
			    sintosa(sa)->sin_addr.s_addr)
				return ifa;
			if (sa->sa_family == AF_INET6 &&
			    !memcmp(&sin6tosa(ifa->ifa_addr)->sin6_addr,
			    &sin6tosa(sa)->sin6_addr,
			    sizeof(struct in6_addr)))
				return ifa;
		}
	}
	return NULL;
}

void
pfk_if_free(struct pfk_ifnet *ifp)
{
	struct ifaddr *ifa;

	while (!TAILQ_EMPTY(&ifp->if_addrlist)) {
		ifa = TAILQ_FIRST(&ifp->if_addrlist);
		TAILQ_REMOVE(&ifp->if_addrlist, ifa, ifa_list);
		kfree(ifa->ifa_addr);
		kfree(ifa);
	}
	kfree(ifp);
}

/* -------- netdev events -------- */
static int
pfk_netdev_event(struct notifier_block *nb, unsigned long ev, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct pfk_ifnet *ifp;
	unsigned long flags;

	switch (ev) {
	case NETDEV_REGISTER:
		ifp = pfk_ifnet_new(dev);
		if (!ifp)
			return NOTIFY_DONE;
		pfk_if_refresh(ifp);
		spin_lock_irqsave(&pfk_ifnet_lock, flags);
		if (idr_find(&pfk_ifnet_idr, dev->ifindex))
			idr_remove(&pfk_ifnet_idr, dev->ifindex);
		if (idr_alloc(&pfk_ifnet_idr, ifp, dev->ifindex,
			dev->ifindex + 1, GFP_ATOMIC) < 0) {
			spin_unlock_irqrestore(&pfk_ifnet_lock, flags);
			kfree(ifp);
			return NOTIFY_DONE;
		}
		spin_unlock_irqrestore(&pfk_ifnet_lock, flags);
		pfk_if_event_call(PFK_IF_EV_ADD, ifp);
		break;
	case NETDEV_UNREGISTER:
		spin_lock_irqsave(&pfk_ifnet_lock, flags);
		ifp = idr_find(&pfk_ifnet_idr, dev->ifindex);
		if (ifp) {
			ifp->alive = 0;
			idr_remove(&pfk_ifnet_idr, dev->ifindex);
			pfk_if_event_call(PFK_IF_EV_DEL, ifp);
			call_rcu(&ifp->rcu, pfk_ifnet_release);
		}
		spin_unlock_irqrestore(&pfk_ifnet_lock, flags);
		break;
	case NETDEV_CHANGE:
	case NETDEV_CHANGENAME:
	case NETDEV_CHANGEMTU:
		ifp = pfk_ifp_bydev(dev);
		if (ifp)
			pfk_if_refresh(ifp);
		break;
	case NETDEV_DOWN:
	case NETDEV_UP: {
		ifp = pfk_ifp_bydev(dev);
		if (ifp)
			pfk_if_refresh(ifp);
		break;
	}
	}
	/* PFLINUX: no egress refresh here - this notifier runs under
	 * rtnl_lock and the FIB lookup behind it would take rtnl again
	 * (self-deadlock).  The address poll (pfk_if_addr_poll_work) refreshes
	 * the egress interface from the workqueue instead, within a second. */
	return NOTIFY_DONE;
}

static struct notifier_block pfk_netdev_nb = {
	.notifier_call = pfk_netdev_event,
};

/* PF engine notification: PFK_IF_EV_{ADD,DEL,ADDR,EGRESS} (see pfk_if.h).
 * Delivered from the pfk_if workqueue, never from atomic context. */
static void (*pfk_if_event_cb)(int kind, struct pfk_ifnet *ifp);

/*
 * PFLINUX: netdev notifiers run with rtnl held; taking NET_LOCK there
 * deadlocks (AB-BA) against control paths that hold NET_LOCK and touch
 * rtnl (dev_get_by_name etc.).  Interface attach/detach is therefore
 * deferred to an ordered workqueue (rtnl-free context) preserving order.
 */
struct pfk_if_event {
	struct work_struct	work;
	int			kind;
	struct pfk_ifnet	*ifp;
};

static struct workqueue_struct *pfk_if_wq;

static void
pfk_if_event_work(struct work_struct *w)
{
	struct pfk_if_event *ev = container_of(w, struct pfk_if_event, work);

	if (pfk_if_event_cb)
		pfk_if_event_cb(ev->kind, ev->ifp);
	kfree(ev);
}

static void
pfk_if_event_call(int kind, struct pfk_ifnet *ifp)
{
	struct pfk_if_event *ev;

	if (!pfk_if_event_cb || !pfk_if_wq)
		return;
	ev = kmalloc(sizeof(*ev), GFP_ATOMIC);
	if (!ev)
		return;
	INIT_WORK(&ev->work, pfk_if_event_work);
	ev->kind = kind;
	ev->ifp = ifp;
	queue_work(pfk_if_wq, &ev->work);
}

void
pfk_if_set_event_cb(void (*cb)(int kind, struct pfk_ifnet *ifp))
{
	pfk_if_event_cb = cb;
}

/*
 * PFLINUX: address-change detection.
 *
 * PF's dynamic-address pools ("nat-to (eth2)", "nat-to (egress)", ...) are
 * resolved by the kernel from the interface's *current* addresses, but the
 * table behind them was only rebuilt when an interface was attached or a
 * group changed - never when an address was added or removed.  A ruleset
 * applied before the WAN address existed therefore kept an empty pool (no
 * translation at all) and one applied before a DHCP change kept the old
 * address, until the next ruleset reload.
 *
 * The kernel gives modules no IP-address notification (see
 * pfk_if_addr_fingerprint()), so the compat layer polls the interfaces
 * once a second and hands the change to the PF engine, which re-publishes
 * the affected pools.
 */
#define PFK_IF_ADDR_POLL_MS	1000

static void pfk_if_addr_poll_work(struct work_struct *w);
static DECLARE_DELAYED_WORK(pfk_if_addr_poll_dwork, pfk_if_addr_poll_work);
static bool pfk_if_addr_polling;

static void
pfk_if_addr_poll_one(struct pfk_ifnet *ifp)
{
	u32 fp = pfk_if_addr_fingerprint(ifp->dev);

	if (fp == ifp->if_addr_fp)
		return;

	/* the addresses changed: republish the snapshot and tell PF */
	pfk_if_addrlist_refresh(ifp);
	pfk_if_event_call(PFK_IF_EV_ADDR, ifp);
}

static void
pfk_if_addr_poll_work(struct work_struct *w)
{
	if (!pfk_if_addr_polling)
		return;

	pfk_if_iterate(pfk_if_addr_poll_one);
	pfk_egress_refresh();
	queue_delayed_work(pfk_if_wq, &pfk_if_addr_poll_dwork,
	    msecs_to_jiffies(PFK_IF_ADDR_POLL_MS));
}

void
pfk_if_iterate(void (*cb)(struct pfk_ifnet *ifp))
{
	struct pfk_ifnet *ifp;
	int id = 0;

	while ((ifp = pfk_if_next_from(&id)) != NULL)
		cb(ifp);
}

/* -------- egress -------- */
void
pfk_egress_refresh(void)
{
	struct pfk_ifnet *ifp = NULL, *old = pfk_egress;
	int idx;

	/* PFLINUX: default route lookup for 0.0.0.0 via FIB.  No RCU read
	 * side here: the dst the helper returns is released inside it, and
	 * ip_route_output_key() may sleep (it allocates), so a sleeping lock
	 * would be needed anyway - this runs from the pfk_if workqueue. */
	/* PFLINUX: the FIB lookup can take rtnl, so it must not run under
	 * rtnl_lock - which is exactly how register_netdevice_notifier()
	 * calls its notifiers.  That is why the notifier path no longer
	 * refreshes the egress interface (the address poll does it instead).
	 */
	idx = ip_rt_get_default_ifindex(&init_net);	/* helper, P1 export */
	ifp = idx ? pfk_ifp_byindex(idx) : NULL;
	/* the old interface must not keep the BSD IFFX_EGRESS flag when the
	 * default route moved to another one */
	if (old != NULL && old != ifp)
		old->if_xflags &= ~0x00000001;
	if (ifp)
		ifp->if_xflags |= 0x00000001;	/* BSD IFFX_EGRESS value */
	pfk_egress = ifp;

	/* the emulated "egress" group follows the default-route interface:
	 * tell PF so the pools bound to it are re-published */
	if (old != ifp)
		pfk_if_event_call(PFK_IF_EV_EGRESS, ifp);
}

struct pfk_ifnet *
pfk_egress_if(void)
{
	return pfk_egress;
}

/* -------- iteration -------- */
struct pfk_ifnet *
pfk_if_first(void)
{
	int id = 0;

	return pfk_if_next_from(&id);
}
/* callers keep id on stack; helper returns next or NULL */
struct pfk_ifnet *
pfk_if_next_from(int *idp)
{
	struct pfk_ifnet *ifp;
	unsigned long flags;

	spin_lock_irqsave(&pfk_ifnet_lock, flags);
	ifp = idr_get_next(&pfk_ifnet_idr, idp);
	spin_unlock_irqrestore(&pfk_ifnet_lock, flags);
	if (ifp)
		(*idp)++;
	return ifp && ifp->alive ? ifp : (ifp ? pfk_if_next_from(idp) : NULL);
}

struct pfk_ifnet *
pfk_if_next(struct pfk_ifnet *ifp)
{
	int id = ifp ? ifp->if_index : 0;

	return pfk_if_next_from(&id);
}

int
pfk_if_init(void)
{
	struct net_device *dev;
	int err;

	pfk_if_wq = alloc_ordered_workqueue("pfk_if", 0);
	if (!pfk_if_wq)
		return -ENOMEM;
	err = register_netdevice_notifier(&pfk_netdev_nb);
	if (err) {
		destroy_workqueue(pfk_if_wq);
		pfk_if_wq = NULL;
		return err;
	}
	/* populate existing devices */
	rtnl_lock();
	for_each_netdev(&init_net, dev) {
		struct pfk_ifnet *ifp = pfk_ifnet_new(dev);
		unsigned long flags;

		if (!ifp)
			continue;
		spin_lock_irqsave(&pfk_ifnet_lock, flags);
		if (!idr_find(&pfk_ifnet_idr, dev->ifindex))
			idr_alloc(&pfk_ifnet_idr, ifp, dev->ifindex,
				dev->ifindex + 1, GFP_KERNEL);
		else
			ifp = NULL;	/* already tracked by notifier */
		spin_unlock_irqrestore(&pfk_ifnet_lock, flags);
		if (ifp) {
			pfk_if_refresh(ifp);
			pfk_if_event_call(1, ifp);
		}
	}
	rtnl_unlock();

	/* PFLINUX: the egress interface is resolved by the address poll
	 * (process context, no rtnl held) - a FIB lookup during module
	 * init deadlocks: register_netdevice_notifier() calls its
	 * notifiers under rtnl_lock and the lookup wants rtnl itself. */

	/* watch the interfaces' addresses (no in-module notification exists) */
	pfk_if_addr_polling = true;
	queue_delayed_work(pfk_if_wq, &pfk_if_addr_poll_dwork,
	    msecs_to_jiffies(PFK_IF_ADDR_POLL_MS));
	return 0;
}

void
pfk_if_exit(void)
{
	pfk_if_addr_polling = false;
	if (pfk_if_wq) {
		cancel_delayed_work_sync(&pfk_if_addr_poll_dwork);
		flush_workqueue(pfk_if_wq);
		destroy_workqueue(pfk_if_wq);
		pfk_if_wq = NULL;
	}
	struct pfk_ifnet *ifp;
	int id = 0;
	unsigned long flags;

	unregister_netdevice_notifier(&pfk_netdev_nb);
	spin_lock_irqsave(&pfk_ifnet_lock, flags);
	while ((ifp = idr_get_next(&pfk_ifnet_idr, &id)) != NULL) {
		ifp->alive = 0;
		idr_remove(&pfk_ifnet_idr, id);
		kfree(ifp);
		id = 0;
	}
	spin_unlock_irqrestore(&pfk_ifnet_lock, flags);
}
