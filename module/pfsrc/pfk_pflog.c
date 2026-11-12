/*
 * PF-on-Linux: pflog backend (ISC 2026)
 *
 * OpenBSD logs matched packets to the pflog(4) *network interface*, which
 * pflogd(8) reads through bpf(4).  Linux has neither if_pflog-style virtual
 * interfaces exposed to bpf nor a DLT_PFLOG consumer, so this port offers the
 * same record format over a character device:
 *
 *	/dev/pflog   (0600)  - read() returns one record at a time:
 *			       struct pflog_hdr followed by the captured
 *			       packet bytes (truncated to PFLOG_COPY_SIZE).
 *
 * poll() reports readable when a record is queued.  A single consumer is the
 * real use case (pflogd), which is why a chardev - not netlink multicast -
 * was chosen; the deviation is documented in docs/porting-notes.md.
 *
 * The records are byte-compatible with OpenBSD's pflog(4) so tcpdump/wireshark
 * read the pcap with linktype DLT_PFLOG (117).
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/timekeeping.h>
#include <linux/uaccess.h>
#include <linux/netdevice.h>

#include <pfk.h>
#include <pfk_api.h>

/* OpenBSD sys/net/pflog.h */
#define PFLOG_IFNAMSIZ		16
#define PFLOG_RULESET_NAME_SIZE	16
#define PFLOG_HDRLEN		64	/* sizeof(struct pfk_pflog_hdr) */
#define PFLOG_COPY_SIZE		160

/* byte-compatible with OpenBSD's <net/pflog.h> (sizeof == 64) */
struct pfk_pflog_hdr {
	struct {
		s64		tv_sec;
		s64		tv_usec;
	} pflog_ts;
	u8			pflog_af;
	u8			pflog_reason;
	u8			pflog_ifname[PFLOG_IFNAMSIZ];
	u8			pflog_ruleset[PFLOG_RULESET_NAME_SIZE];
	u8			pflog_nr;
	u8			pflog_anc;
	u8			pflog_flags;
	u8			pflog_pad[9];
	u16			pflog_len;	/* captured packet length */
} __packed;

struct pfk_pflog_rec {
	struct list_head	list;
	size_t			len;	/* header + payload */
	char			data[];	/* pfk_pflog_hdr, then the packet */
};

#define PFK_PFLOG_MAX_QUEUE	256u
#define PFK_PFLOG_MAX_BYTES	(1u << 20)

static LIST_HEAD(pflog_list);
static DEFINE_SPINLOCK(pflog_lock);
static DECLARE_WAIT_QUEUE_HEAD(pflog_wq);
static unsigned long pflog_queued, pflog_bytes, pflog_dropped;
static u64 pflog_seq;

/*
 * Queue one record.  Called from the packet path (process context here: the
 * data path runs in pfk_netisr_wq), so a GFP_ATOMIC allocation and a bounded
 * queue keep it safe even if a future caller appears in softirq context.
 */
static void
pfk_pflog_put(struct pfk_pflog_hdr *hdr, const void *pkt, size_t pktlen)
{
	struct pfk_pflog_rec *rec;
	size_t len = sizeof(*hdr) + pktlen;
	unsigned long flags;

	if (READ_ONCE(pflog_queued) >= PFK_PFLOG_MAX_QUEUE ||
	    READ_ONCE(pflog_bytes) + len > PFK_PFLOG_MAX_BYTES) {
		pflog_dropped++;
		return;
	}

	rec = kmalloc(struct_size(rec, data, len), GFP_ATOMIC);
	if (rec == NULL) {
		pflog_dropped++;
		return;
	}
	rec->len = len;
	memcpy(rec->data, hdr, sizeof(*hdr));
	memcpy(rec->data + sizeof(*hdr), pkt, pktlen);

	spin_lock_irqsave(&pflog_lock, flags);
	list_add_tail(&rec->list, &pflog_list);
	pflog_queued++;
	pflog_bytes += len;
	pflog_seq++;
	spin_unlock_irqrestore(&pflog_lock, flags);

	wake_up_interruptible(&pflog_wq);
}

/* copy the leading bytes of the packet (the mbuf chain may be a wrapper) */
static size_t
pfk_pflog_pull(struct mbuf *m, char *buf, size_t want)
{
	struct sk_buff *skb = m ? m_to_skb(m) : NULL;
	size_t n;
	int off;

	if (skb == NULL)
		return 0;
	/*
	 * BSD logs from the network header; pf may already have advanced the
	 * mbuf/skb data pointer to the transport header while parsing, so
	 * anchor explicitly at the IP header instead of at skb->data.
	 */
	off = skb_network_offset(skb);
	if (off < 0)
		off = 0;
	if (off > skb->len)
		return 0;
	n = min_t(size_t, want, skb->len - off);
	if (skb_copy_bits(skb, off, buf, n))
		return 0;
	return n;
}

/* ---------------- pflog_packet() (called by the ported pf.c) ---------------- */

int
pflog_packet(struct pf_pdesc *pd, u_int8_t reason, struct pf_rule *rm,
    struct pf_rule *am, struct pf_ruleset *ruleset, struct pf_rule *cm)
{
	struct pfk_pflog_hdr hdr;
	char pkt[PFLOG_COPY_SIZE];
	struct pf_rule *r;
	size_t n;

	if (pd == NULL || pd->m == NULL)
		return (0);

	memset(&hdr, 0, sizeof(hdr));
	{
		struct timespec64 ts;

		ktime_get_real_ts64(&ts);
		hdr.pflog_ts.tv_sec = ts.tv_sec;
		hdr.pflog_ts.tv_usec = ts.tv_nsec / 1000;
	}
	hdr.pflog_af = pd->af;
	hdr.pflog_reason = reason;

	r = cm ? cm : (am ? am : rm);
	if (r) {
		strscpy((char *)hdr.pflog_ifname,
		    pd->kif ? pd->kif->pfik_name : "",
		    sizeof(hdr.pflog_ifname));
		strscpy((char *)hdr.pflog_ruleset,
		    (ruleset && ruleset->anchor) ? ruleset->anchor->name : "",
		    sizeof(hdr.pflog_ruleset));
		hdr.pflog_nr = r->nr;
		hdr.pflog_anc = 0;
		hdr.pflog_flags = r->log;
	}

	n = pfk_pflog_pull(pd->m, pkt, sizeof(pkt));
	hdr.pflog_len = cpu_to_be16(n);	/* BSD stores it in network order */

	pfk_pflog_put(&hdr, pkt, n);
	return (0);
}

/* ---------------- /dev/pflog ---------------- */

static int
pfk_pflog_open(struct inode *inode, struct file *filp)
{
	filp->private_data = NULL;
	return nonseekable_open(inode, filp);
}

static ssize_t
pfk_pflog_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
	struct pfk_pflog_rec *rec = filp->private_data;
	unsigned long flags;
	size_t off, n;

	spin_lock_irqsave(&pflog_lock, flags);
	if (rec == NULL) {
		if (list_empty(&pflog_list)) {
			spin_unlock_irqrestore(&pflog_lock, flags);
			if (filp->f_flags & O_NONBLOCK)
				return -EAGAIN;
			if (wait_event_interruptible(pflog_wq,
			    !list_empty(&pflog_list)))
				return -ERESTARTSYS;
			spin_lock_irqsave(&pflog_lock, flags);
			if (list_empty(&pflog_list)) {
				spin_unlock_irqrestore(&pflog_lock, flags);
				return -EAGAIN;
			}
		}
		rec = list_first_entry(&pflog_list, struct pfk_pflog_rec, list);
		list_del(&rec->list);
		pflog_queued--;
		pflog_bytes -= rec->len;
		filp->private_data = rec;
	}
	spin_unlock_irqrestore(&pflog_lock, flags);

	/* partial reads resume through *ppos (record is pinned to the file) */
	off = *ppos;
	if (off >= rec->len) {
		kfree(rec);
		filp->private_data = NULL;
		*ppos = 0;
		return 0;
	}
	n = min(count, rec->len - off);
	if (copy_to_user(buf, rec->data + off, n))
		return -EFAULT;
	*ppos += n;
	if (*ppos == rec->len) {
		kfree(rec);
		filp->private_data = NULL;
		*ppos = 0;
	}
	return n;
}

static __poll_t
pfk_pflog_poll(struct file *filp, poll_table *wait)
{
	__poll_t mask = 0;

	poll_wait(filp, &pflog_wq, wait);
	if (filp->private_data != NULL)
		mask |= EPOLLIN | EPOLLRDNORM;
	else if (!list_empty(&pflog_list))
		mask |= EPOLLIN | EPOLLRDNORM;
	return mask;
}

static long
pfk_pflog_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct pfk_pflog_stat st;

	switch (cmd) {
	case PFKIOCGETPFLOGSTAT:
		st.queued = pflog_queued;
		st.bytes = pflog_bytes;
		st.dropped = pflog_dropped;
		st.seq = pflog_seq;
		if (copy_to_user((void __user *)arg, &st, sizeof(st)))
			return -EFAULT;
		return 0;
	default:
		return -ENOTTY;
	}
}

static const struct file_operations pfk_pflog_fops = {
	.owner		= THIS_MODULE,
	.open		= pfk_pflog_open,
	.read		= pfk_pflog_read,
	.poll		= pfk_pflog_poll,
	.unlocked_ioctl	= pfk_pflog_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
};

static struct miscdevice pfk_pflog_dev = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "pflog",
	.fops		= &pfk_pflog_fops,
	.mode		= 0600,
};

int
pfk_pflog_register(void)
{
	return misc_register(&pfk_pflog_dev);
}

void
pfk_pflog_unregister(void)
{
	struct pfk_pflog_rec *rec, *tmp;

	misc_deregister(&pfk_pflog_dev);
	spin_lock(&pflog_lock);
	list_for_each_entry_safe(rec, tmp, &pflog_list, list) {
		list_del(&rec->list);
		kfree(rec);
	}
	pflog_queued = 0;
	pflog_bytes = 0;
	spin_unlock(&pflog_lock);
}
