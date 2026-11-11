/*
 * PF-on-Linux ifnet/address registry (ISC 2026).
 * Maintains BSD-flavored struct pfk_ifnet views over net_device, refreshed
 * address snapshots, egress tracking, and name/index lookups. All ops under
 * NET_LOCK (read for lookups, write for list mutation) except notifier which
 * uses its own spinlock + work.
 */
#include <pfk.h>

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
	/* address snapshot: rebuild list from Linux in_ifaddr/inet6_ifaddr */
	pfk_if_addrlist_refresh(ifp);
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
		break;
	case NETDEV_UNREGISTER:
		spin_lock_irqsave(&pfk_ifnet_lock, flags);
		ifp = idr_find(&pfk_ifnet_idr, dev->ifindex);
		if (ifp) {
			ifp->alive = 0;
			idr_remove(&pfk_ifnet_idr, dev->ifindex);
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
		pfk_egress_refresh();
		break;
	}
	}
	pfk_egress_refresh();
	return NOTIFY_DONE;
}

static struct notifier_block pfk_netdev_nb = {
	.notifier_call = pfk_netdev_event,
};

/* -------- egress -------- */
void
pfk_egress_refresh(void)
{
	struct pfk_ifnet *ifp = NULL;
	struct net_device *dev;

	/* PFLINUX: default route lookup for 0.0.0.0 via FIB */
	rcu_read_lock();
	dev = ip_rt_get_default_dev(&init_net);	/* helper, P1 export */
	rcu_read_unlock();
	ifp = pfk_ifp_bydev(dev);
	if (ifp)
		ifp->if_xflags |= 0x00000001;	/* BSD IFFX_EGRESS value */
	pfk_egress = ifp;
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
	int id = -1;

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
	int id = ifp ? ifp->if_index : -1;

	return pfk_if_next_from(&id);
}

int
pfk_if_init(void)
{
	struct net_device *dev;
	int err;

	err = register_netdevice_notifier(&pfk_netdev_nb);
	if (err)
		return err;
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
		if (ifp)
			pfk_if_refresh(ifp);
	}
	rtnl_unlock();
	pfk_egress_refresh();
	return 0;
}

void
pfk_if_exit(void)
{
	struct pfk_ifnet *ifp;
	int id = -1;
	unsigned long flags;

	unregister_netdevice_notifier(&pfk_netdev_nb);
	spin_lock_irqsave(&pfk_ifnet_lock, flags);
	while ((ifp = idr_get_next(&pfk_ifnet_idr, &id)) != NULL) {
		ifp->alive = 0;
		idr_remove(&pfk_ifnet_idr, id);
		kfree(ifp);
		id = -1;
	}
	spin_unlock_irqrestore(&pfk_ifnet_lock, flags);
}
