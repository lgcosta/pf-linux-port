/*
 * PF-on-Linux: AF_DIVERT socket family (ISC 2026)
 *
 * Implements OpenBSD's divert(4) as a runtime-registered protocol family
 * inside the PF module:  PF_DIVERT = 32 (BSD value; the unused legacy
 * PF_IUCV slot on Linux, and NPROTO = AF_MAX = 45 admits it),
 * IPPROTO_DIVERT = 254.  No core patch is required — families are
 * registered with sock_register() at module init.
 *
 * Userspace ABI (see docs/en/divert-wireformat.md):
 *   fd = socket(PF_DIVERT, SOCK_RAW, IPPROTO_DIVERT);
 *   struct sockaddr_in b = { .sin_family = AF_INET, .sin_port = port };
 *   bind(fd, &b);                      # binds the divert "port"
 *   n = recv(fd, pkt, ...)             # full IP packet incl. header
 *   send(fd, pkt, n)                   # reinject per BSD semantics
 *
 * Re-injection mirrors OpenBSD netinet/ip_divert.c:divert_output():
 *   type PF_DIVERT_TO  -> local delivery as if arrived on the recorded
 *                         interface (netif_rx)
 *   plain divert tag   -> route again (dst output), matching BSD ip_output
 *
 * Semantics notes (PFLINUX, docs/porting-notes.md):
 *   - queue selection scans the bound list under pfk_divert_lock;
 *   - full-socket drop counts divs_fullsock like BSD;
 *   - no socket bound to the port: divs_noport + drop (BSD identical).
 */
#include <pfk.h>
#include <pfk_api.h>
#include <net/sock.h>

#define PF_DIVERT_FAMILY	32	/* BSD PF_DIVERT */
#ifndef IPPROTO_DIVERT
#define IPPROTO_DIVERT		254
#endif

/* control message codes reported to userspace (sendmsg accepts them back) */
#define PFDIVERT_CMSG_IFINDEX	1
#define PFDIVERT_CMSG_DIR	2
#define PFDIVERT_CMSG_TYPE	3

struct pfk_divert_pkt {
	struct list_head	node;
	struct sk_buff		*skb;
	int			dir;
	int			type;
	u16			port;
	int			ifindex;
};

struct pfk_divert_stat {
	u64	divs_ipackets;
	u64	divs_noport;
	u64	divs_fullsock;
	u64	divs_opackets;
	u64	divs_errors;
};

static const struct proto_ops pfk_divert_ops;

struct pfk_divert_sock {
	struct list_head	link;		/* global bound list */
	struct list_head	q;
	spinlock_t		lock;
	u16			port;
	u32			qlen;
	int			last_ifindex;	/* device of last queued packet */
	wait_queue_head_t	wq;
	u32			qspace;
	struct sock		*sk;
	struct pfk_divert_stat	stats;
};

static LIST_HEAD(pfk_divert_list);
static DEFINE_SPINLOCK(pfk_divert_lock);
static struct pfk_divert_stat pfk_divert_global;
static struct proto pfk_divert_proto = {
	.name		= "PFK_DIVERT",
	.owner		= THIS_MODULE,
	.obj_size	= sizeof(struct sock),
};

static inline struct pfk_divert_sock *
pfk_ds(const struct sock *sk)
{
	return sk ? (struct pfk_divert_sock *)sk->sk_user_data : NULL;
}

static void
pfk_divert_pkt_free(struct pfk_divert_pkt *dp)
{
	if (!dp)
		return;
	if (dp->skb)
		kfree_skb(dp->skb);
	kfree(dp);
}

/* PF engine entry points (called from the ported pf.c) */
static struct pfk_divert_sock *
pfk_divert_lookup(u_int16_t port)
{
	struct pfk_divert_sock *ds;

	list_for_each_entry(ds, &pfk_divert_list, link) {
		if (ds->port == port)
			return ds;
	}
	return NULL;
}

static int
pfk_divert_queue(struct sk_buff *skb, int dir, int type, u16 port)
{
	struct pfk_divert_sock *ds;
	struct pfk_divert_pkt *dp;
	unsigned long flags;

	dp = kmalloc(sizeof(*dp), GFP_ATOMIC);
	if (!dp) {
		kfree_skb(skb);
		return ENOMEM;
	}
	dp->skb = skb;
	dp->dir = dir;
	dp->type = type;
	dp->port = port;
	dp->ifindex = skb->dev ? skb->dev->ifindex : 0;

	spin_lock_irqsave(&pfk_divert_lock, flags);
	ds = pfk_divert_lookup(port);
	if (ds == NULL) {
		pfk_divert_global.divs_noport++;
		spin_unlock_irqrestore(&pfk_divert_lock, flags);
		pr_info_ratelimited("pfk: divert port %u: no socket bound\n",
			port);
		pfk_divert_pkt_free(dp);
		return ENOENT;
	}
	if (ds->qlen >= ds->qspace) {
		ds->stats.divs_fullsock++;
		pfk_divert_global.divs_fullsock++;
		spin_unlock_irqrestore(&pfk_divert_lock, flags);
		pfk_divert_pkt_free(dp);
		return ENOBUFS;
	}
	spin_lock(&ds->lock);
	list_add_tail(&dp->node, &ds->q);
	ds->qlen++;
	ds->stats.divs_ipackets++;
	ds->last_ifindex = dp->ifindex;
	spin_unlock(&ds->lock);
	spin_unlock_irqrestore(&pfk_divert_lock, flags);

	wake_up_interruptible(&ds->wq);
	return 0;
}

extern struct pf_divert *pf_find_divert(struct mbuf *);

void
divert_packet(struct mbuf *m, int dir, u_int16_t divert_port)
{
	struct sk_buff *skb;
	int type = PF_DIVERT_TO;

	if (!m)
		return;
	{
		struct pf_divert *dv = pf_find_divert(m);

		if (dv) {
			type = dv->type;
			if (divert_port == 0)
				divert_port = dv->port;
		}
	}
	skb = m_to_skb(m);
	if (skb)
		skb_get(skb);	/* divert layer's own reference */
	m->skb = NULL;
	m_freem(m);
	if (skb)
		pfk_divert_queue(skb, dir, type, divert_port);
}

void
divert6_packet(struct mbuf *m, int dir, u_int16_t divert_port)
{
	divert_packet(m, dir, divert_port);
}

/* ---------------- socket ops ---------------- */

static int
pfk_divert_create(struct net *net, struct socket *sock, int protocol, int kern)
{
	struct sock *sk;
	struct pfk_divert_sock *ds;

	if (protocol != IPPROTO_DIVERT && protocol != 0)
		return -EPROTONOSUPPORT;
	if (sock->type != SOCK_RAW)
		return -ESOCKTNOSUPPORT;

	ds = kzalloc(sizeof(*ds), GFP_KERNEL);
	if (!ds)
		return -ENOMEM;
	spin_lock_init(&ds->lock);
	init_waitqueue_head(&ds->wq);
	INIT_LIST_HEAD(&ds->link);
	INIT_LIST_HEAD(&ds->q);
	ds->qspace = 65536 + 100;	/* BSD DIVERT_RECVSPACE */

	sk = sk_alloc(net, PF_DIVERT_FAMILY, GFP_KERNEL, &pfk_divert_proto,
	    kern);
	if (!sk) {
		kfree(ds);
		return -ENOMEM;
	}
	sock_init_data(sock, sk);
	sk->sk_user_data = ds;
	ds->sk = sk;
	sock->state = SS_UNCONNECTED;
	sock->ops = &pfk_divert_ops;	/* forward decl below via struct */
	return 0;
}

static int
pfk_divert_bind(struct socket *sock, struct sockaddr_unsized *uaddr,
	int addr_len)
{
	struct pfk_divert_sock *ds = pfk_ds(sock->sk);
	unsigned long flags;

	if (!ds)
		return -EINVAL;
	if (addr_len < (int)sizeof(struct sockaddr_in))
		return -EINVAL;

	/* BSD divsock stores the port in network order (compared against
	 * inp_lport / pf_divert.port as-is). */
	ds->port = ((struct sockaddr_in *)uaddr)->sin_port;
	if (ds->port == 0)
		return -EADDRNOTAVAIL;	/* divert port 0 not allowed */

	spin_lock_irqsave(&pfk_divert_lock, flags);
	if (!list_empty(&ds->link)) {
		spin_unlock_irqrestore(&pfk_divert_lock, flags);
		return -EADDRINUSE;
	}
	list_add_tail(&ds->link, &pfk_divert_list);
	spin_unlock_irqrestore(&pfk_divert_lock, flags);
	return 0;
}

static int
pfk_divert_recvmsg(struct socket *sock, struct msghdr *msg, size_t len,
	int flags)
{
	struct pfk_divert_sock *ds = pfk_ds(sock->sk);
	struct pfk_divert_pkt *dp;
	struct sk_buff *skb;
	unsigned long iflags;
	int copied;

	if (!ds)
		return -EINVAL;
	if (msg_data_left(msg) == 0)
		return -EINVAL;

	for (;;) {
		spin_lock_irqsave(&ds->lock, iflags);
		if (!list_empty(&ds->q)) {
			dp = list_first_entry(&ds->q, struct pfk_divert_pkt,
			    node);
			list_del_init(&dp->node);
			ds->qlen--;
			spin_unlock_irqrestore(&ds->lock, iflags);
			goto have;
		}
		spin_unlock_irqrestore(&ds->lock, iflags);
		if (flags & MSG_DONTWAIT ||
		    READ_ONCE(sock->sk->sk_shutdown) != 0)
			return -EAGAIN;
		wait_event_interruptible(ds->wq,
		    !list_empty(&ds->q) ||
		    READ_ONCE(sock->sk->sk_shutdown) != 0);
		if (signal_pending(current))
			return -ERESTARTSYS;
	}

have:
	skb = dp->skb;
	copied = min_t(size_t, len, skb->len);
	if (skb_copy_datagram_msg(skb, 0, msg, copied)) {
		pfk_divert_pkt_free(dp);
		return -EFAULT;
	}
	/* metadata as control data (BSD equivalent: sockaddr_div) */
	{
		struct cmsghdr *cm = CMSG_FIRSTHDR(msg);

		if (cm && msg->msg_controllen >= sizeof(struct cmsghdr) + sizeof(int)) {
			msg->msg_controllen = 0;	/* busybox-safe default */
			cm = NULL;
		}
		if (cm) {
			int ifindex = dp->ifindex;

			cm->cmsg_level = SOL_SOCKET;
			cm->cmsg_type = PFDIVERT_CMSG_IFINDEX;
			cm->cmsg_len = CMSG_LEN(sizeof(int));
			memcpy(CMSG_DATA(cm), &ifindex, sizeof(ifindex));
			msg->msg_controllen = CMSG_SPACE(sizeof(int));
		}
	}
	sock_net_set(sock->sk, &init_net);
	ds->stats.divs_opackets++;
	pfk_divert_pkt_free(dp);
	return copied;
}

/*
 * sendmsg: userspace hands the (possibly rewritten) packet back.  The
 * ip header inside the bytes decides the flow; for PF_DIVERT_TO we inject
 * as input on the socket's last-recorded interface, otherwise we route.
 */
static int
pfk_divert_sendmsg(struct socket *sock, struct msghdr *msg, size_t len)
{
	struct pfk_divert_sock *ds = pfk_ds(sock->sk);
	struct sk_buff *skb;
	struct pf_ip *ip;
	int ifindex = 0;
	int type = PF_DIVERT_TO;
	struct cmsghdr *cm;

	if (!ds)
		return -EINVAL;
	if (len > 65535)
		return -EMSGSIZE;

	for (cm = CMSG_FIRSTHDR(msg); cm; cm = CMSG_NXTHDR(msg, cm)) {
		if (cm->cmsg_level != SOL_SOCKET)
			continue;
		if (cm->cmsg_type == PFDIVERT_CMSG_IFINDEX &&
		    cm->cmsg_len >= CMSG_LEN(sizeof(int)))
			memcpy(&ifindex, CMSG_DATA(cm), sizeof(int));
		if (cm->cmsg_type == PFDIVERT_CMSG_TYPE &&
		    cm->cmsg_len >= CMSG_LEN(sizeof(int)))
			memcpy(&type, CMSG_DATA(cm), sizeof(int));
	}

	skb = alloc_skb(len + NET_SKB_PAD + NET_IP_ALIGN, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;
	skb_reserve(skb, NET_SKB_PAD + NET_IP_ALIGN);
	if (memcpy_from_msg(skb_put(skb, len), msg, len)) {
		kfree_skb(skb);
		return -EFAULT;
	}
	skb->protocol = htons(ETH_P_IP);
	if (ifindex <= 0)
		ifindex = ds->last_ifindex;	/* same path the packet came in */

	if (type == PF_DIVERT_TO) {
		/* deliver locally as if arrived on ifindex (default: last
		 * ingress iface seen by this socket's queue) */
		struct net_device *dev = ifindex > 0 ?
			dev_get_by_index(&init_net, ifindex) : NULL;

		if (dev) {
			struct ethhdr *eh;

			skb->dev = dev;
			dev_put(dev);
			skb->mark |= PFK_SKIP_PF;	/* avoid divert loop */
			/*
			 * The receive path of an Ethernet device expects an
			 * L2 header: synthesise one (the packet keeps its
			 * original addresses; the stack only needs the
			 * ethertype to demultiplex).
			 */
			eh = skb_push(skb, ETH_HLEN);
			if (!eh) {
				kfree_skb(skb);
				return -ENOMEM;
			}
			memset(eh, 0, ETH_HLEN);
			memcpy(eh->h_dest, dev->dev_addr, ETH_ALEN);
			memcpy(eh->h_source, dev->dev_addr, ETH_ALEN);
			eh->h_proto = htons(ETH_P_IP);
			skb_reset_mac_header(skb);
			skb->protocol = htons(ETH_P_IP);
			skb_pull(skb, ETH_HLEN);	/* like eth_type_trans() */
			/* data now points at the IP header again */
			skb_reset_network_header(skb);
			netif_rx(skb);
			return (int)len;
		}
		/* no device: route below */
	}

	/* plain divert: route again (BSD divert_output -> ip_output) */
	if (pskb_may_pull(skb, sizeof(*ip))) {
		struct flowi4 fl4 = { };
		struct dst_entry *d;

		ip = (struct pf_ip *)skb->data;
		fl4.daddr = ip->ip_dst.s_addr;
		fl4.saddr = ip->ip_src.s_addr;
		{
			struct rtable *rt = ip_route_output_key(&init_net,
			    &fl4);

			if (IS_ERR(rt))
				rt = NULL;
			d = rt ? &rt->dst : NULL;
		}
		if (d) {
			skb_dst_set(skb, d);
			skb_reset_network_header(skb);
			if (!skb->dev)
				skb->dev = skb_dst(skb)->dev;
			d->output(&init_net, NULL, skb);
			return (int)len;
		}
	}
	kfree_skb(skb);
	ds->stats.divs_errors++;
	return -EHOSTUNREACH;
}

static int
pfk_divert_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct pfk_divert_sock *ds;
	unsigned long flags;

	if (!sk)
		return 0;
	ds = pfk_ds(sk);
	if (ds) {
		spin_lock_irqsave(&pfk_divert_lock, flags);
		if (!list_empty(&ds->link))
			list_del_init(&ds->link);
		spin_unlock_irqrestore(&pfk_divert_lock, flags);
		while (!list_empty(&ds->q)) {
			struct pfk_divert_pkt *dp = list_first_entry(&ds->q,
			    struct pfk_divert_pkt, node);

			list_del_init(&dp->node);
			pfk_divert_pkt_free(dp);
		}
		kfree(ds);
		sk->sk_user_data = NULL;
	}
	sock_orphan(sk);
	sock_put(sk);
	return 0;
}

static __poll_t
pfk_divert_poll(struct file *file, struct socket *sock,
	poll_table *wait)
{
	struct pfk_divert_sock *ds = pfk_ds(sock->sk);
	__poll_t mask = 0;

	if (ds)
		poll_wait(file, &ds->wq, wait);
	else
		sock_poll_wait(file, sock, wait);
	if (ds && !list_empty(&ds->q))
		mask |= EPOLLIN | EPOLLRDNORM;
	return mask;
}

static int
pfk_divert_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct pfk_divert_sock *ds = pfk_ds(sock->sk);

	if (!ds)
		return -EINVAL;
	switch (cmd) {
	case 3: {	/* DIVERTCTL_STATS-ish: export queue stats */
		struct pfk_divert_stat st;
		unsigned long flags;

		spin_lock_irqsave(&ds->lock, flags);
		st = ds->stats;
		spin_unlock_irqrestore(&ds->lock, flags);
		if (copy_to_user((void __user *)arg, &st, sizeof(st)))
			return -EFAULT;
		return 0;
	}
	default:
		return -ENOIOCTLCMD;
	}
}

static const struct proto_ops pfk_divert_ops = {
	.family		= PF_DIVERT_FAMILY,
	.owner		= THIS_MODULE,
	.release	= pfk_divert_release,
	.bind		= pfk_divert_bind,
	.connect	= sock_no_connect,
	.socketpair	= sock_no_socketpair,
	.accept		= sock_no_accept,
	.getname	= sock_no_getname,
	.poll		= pfk_divert_poll,
	.ioctl		= pfk_divert_ioctl,
	.listen		= sock_no_listen,
	.shutdown	= sock_no_shutdown,
	.sendmsg	= pfk_divert_sendmsg,
	.recvmsg	= pfk_divert_recvmsg,
	.mmap		= sock_no_mmap,
};

static const struct net_proto_family pfk_divert_family = {
	.family	= PF_DIVERT_FAMILY,
	.create	= pfk_divert_create,
	.owner	= THIS_MODULE,
};

int
pfk_divert_sock_register(void)
{
	return sock_register(&pfk_divert_family);
}

void
pfk_divert_sock_unregister(void)
{
	sock_unregister(PF_DIVERT_FAMILY);
}
