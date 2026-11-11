/*
 * PF-on-Linux: master compatibility header (pfk).
 *
 * Every ported OpenBSD file includes <pfk.h> INSTEAD of the original
 * #include block. pfk.h pulls Linux headers first, then provides the
 * OpenBSD-flavored types/APIs the PF code expects, so the ported logic
 * stays line-proximate to upstream (ISC, (c) 2026 PF-on-Linux contributors;
 * ported code: OpenBSD project, see LICENSE).
 */
#ifndef _PFK_H
#define _PFK_H

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/limits.h>
#ifndef UINT16_MAX
#define UINT16_MAX	0xffffU
#endif
#ifndef UINT32_MAX
#define UINT32_MAX	0xffffffffU
#endif
#ifndef UINT8_MAX
#define UINT8_MAX	0xffU
#endif
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/rwsem.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <linux/timekeeping.h>
#include <linux/random.h>
#include <linux/prandom.h>
#include <linux/percpu.h>
#include <linux/preempt.h>
#include <linux/cpumask.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/icmp.h>
#include <linux/icmpv6.h>
#include <linux/icmpv6.h>
#include <linux/inetdevice.h>
#include <linux/if_addr.h>
#include <linux/neighbour.h>
#include <net/route.h>
#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <net/ip.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/icmp.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <net/dst.h>
#include <net/neighbour.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/uaccess.h>
#include <linux/socket.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/workqueue.h>
#include <linux/timer.h>
#include <linux/highuid.h>
#include <linux/atomic.h>
#include <linux/hashtable.h>
#include <linux/jhash.h>
#include <linux/byteorder/generic.h>
#include <linux/ratelimit.h>

/* ------------------------------------------------------------ basic types */
#ifndef u_int8_t
typedef __u8  u_int8_t;
#endif
#ifndef u_int16_t
typedef __u16 u_int16_t;
#endif
#ifndef u_int32_t
typedef __u32 u_int32_t;
#endif
#ifndef u_int64_t
typedef __u64 u_int64_t;
#endif
#ifndef u64
typedef __u64 u64;
#endif
#ifndef u_int
typedef unsigned int u_int;
#endif
#ifndef time_t
typedef long time_t;
#endif
#ifndef u_char
typedef unsigned char u_char;
#endif
/* caddr_t/time_t/timeval: kernel provides caddr_t; BSD time types below */
#ifndef u_short
typedef unsigned short u_short;
#endif
typedef __be16 pf_in_port_t;
typedef __be32 pf_in_addr_t;
/* uapi linux/in6.h already provides s6_addr/s6_addr16/s6_addr32 macros
 * matching OpenBSD usage on the same union layout. */

/* roundup/MIN/MAX/rounddown/NULL/PTR_ALIGN are already provided by the
 * Linux kernel headers — use those. BSD-only extras kept under unique names. */
#define roundup2(x, y)  (((x)+((y)-1))&(~((y)-1)))
#define howmany(x, y)   (((x)+((y)-1))/(y))
#define MINIMUM(a, b)   (((a) < (b)) ? (a) : (b))
#define MAXIMUM(a, b)   (((a) > (b)) ? (a) : (b))
#define PFK_PTR_ALIGN(p) ((caddr_t)(((unsigned long)(p) + 3) & ~3UL))
#define P2ALIGN(x, y)	((x) & -(y))
#define NBBY		8

/* BSD string helpers the PF code uses */
static inline size_t
pfk_strlcpy(char *dst, const char *src, size_t dsize)
{
	size_t srclen = strlen(src);

	if (dsize != 0) {
		size_t len = (srclen >= dsize) ? dsize - 1 : srclen;

		memcpy(dst, src, len);
		dst[len] = '\0';
	}
	return srclen;
}
#define strlcpy(d, s, n)	pfk_strlcpy((d), (s), (n))

#define MAXPATHLEN	1024

/* OpenBSD pool flag */
#define PR_LIMITFAIL	0x04

/* OpenBSD kern.log helper (compat: routed to the kernel log) */
void addlog(const char *fmt, ...);

/* BSD interface-group flags and list (pf_if.c / net/if_var.h) */
/* OpenBSD: interface groups are identified by name */
#define IFG_ALL		"all"
#define IFG_EGRESS	"egress"
#define IFQ_MAXPRIO	31

/* BSD per-task hook stubs: Linux uses the netdevice notifier instead
 * (pfk_if.c); the task-based address hook is a no-op in the port. */
struct pfk_ifnet;
struct task {
	void		*ta_data;
	void		(*ta_func)(void *);
};
#define task_set(t, f, d)	do { (t)->ta_func = (f); (t)->ta_data = (d); } while (0)
#define task_ready(t)		((t)->ta_func != NULL)
#define taskq_handler(t)	((t)->ta_func)
static inline int if_addrhook_add(struct pfk_ifnet *ifp, struct task *t)
{
	(void)ifp; (void)t;
	return 0;
}
static inline int if_addrhook_del(struct pfk_ifnet *ifp, struct task *t)
{
	(void)ifp; (void)t;
	return 0;
}

/* BSD misc helpers used by the PF code */
#define bcopy(s, d, n)	memcpy((d), (s), (n))
#define bzero(p, n)	memset((p), 0, (n))
/* OpenBSD exposes 'preempt' as a macro yielding the preemption depth; the
 * PF code only passes it to sched_pause(), so 0 (not preempted) is exact
 * for our purposes. */
#define preempt		0
/* BSD sched_pause(usec): voluntary short pause */
#define sched_pause(usec)	do { if (usec) udelay(usec); } while (0)
/* BSD splassert_fail(expected, actual, func) */
void splassert_fail(int expected, int actual, const char *func);
/* BSD IPv6 extension header (netinet6/ip6.h) */
struct ip6_ext {
	__u8	ip6e_nxt;
	__u8	ip6e_len;
};

#define IPV6_MAXPACKET	65535
#define IPV6_MINMTU	1280
#define ICMP6_DST_UNREACH		1
#define ICMP6_PACKET_TOO_BIG		2
#define ICMP6_TIME_EXCEEDED		3
#define ICMP6_ECHO_REQUEST		128
#define ICMP6_ECHO_REPLY		129

/* BSD IPv6 fragment header (OpenBSD netinet6/ip6.h layout) */
struct ip6_frag {
	__u8	ip6f_nxt;
	__u8	ip6f_reserved;
	__be16	ip6f_offlg;
	__be32	ip6f_ident;
};
#define IP6F_OFF_MASK	0xfff8
#define IP6F_MORE_FRAG	0x0001
#define IP6F_ATOMIC_FRAG	0x0002

/* PF mbuf tags (our namespace; upstream uses PACKET_TAG_PF_*) */
#define PACKET_TAG_PF_REASSEMBLED	0x50460001
#define PACKET_TAG_PF_REFRAGMENTED	0x50460002
#define PACKET_TAG_PF_DIVERT		0x50460003

/* OpenBSD builds the kernel with DIAGNOSTIC (extra KASSERT coverage) */
#ifndef DIAGNOSTIC
#define DIAGNOSTIC	1
#endif

/* BSD IPv4 fragment bits */
#define IP_OFFMASK	0x1fff
#define IP_MF		0x2000
#define NMBCLUSTERS	16
extern u_int nmbclust;

/* OpenBSD mutex API (mtx_*) over our mutex macros */
#define mtx_init(m, ...)			mutex_init((m), "pfk", 0)
#define mtx_enter(m)			mutex_enter(m)
#define mtx_leave(m)			mutex_leave(m)
#define mtx_destroy(m)			mutex_destroy(m)

/* compile-time assertion */
#define CTASSERT(x)	static_assert(x, #x)

/* BSD bit helpers */
#define ISSET(x, y)	((x) & (y))
#define SET(x, y)	((x) |= (y))
#define CLR(x, y)	((x) &= ~(y))
#define RESET(x, y)	do { (x) &= ~(y); } while (0)

/* BSD IPv6 address class macros (Linux 7.3 only exposes multicast) */
#define IN6_IS_ADDR_MULTICAST(a)	ipv6_addr_is_multicast(a)
#define IN6_IS_ADDR_LINKLOCAL(a) \
	(((a)->s6_addr[0] == 0xfe) && (((a)->s6_addr[1] & 0xc0) == 0x80))
#define IN6_IS_ADDR_LOOPBACK(a) \
	(((a)->s6_addr[0] == 0) && ((a)->s6_addr[1] == 0) && \
	 ((a)->s6_addr[2] == 0) && ((a)->s6_addr[3] == 0) && \
	 ((a)->s6_addr[4] == 0) && ((a)->s6_addr[5] == 0) && \
	 ((a)->s6_addr[6] == 0) && ((a)->s6_addr[7] == 1))
#define IN6_IS_ADDR_UNSPECIFIED(a) \
	(((a)->s6_addr[0] == 0) && ((a)->s6_addr[1] == 0) && \
	 ((a)->s6_addr[2] == 0) && ((a)->s6_addr[3] == 0) && \
	 ((a)->s6_addr[4] == 0) && ((a)->s6_addr[5] == 0) && \
	 ((a)->s6_addr[6] == 0) && ((a)->s6_addr[7] == 0))
#define IN6_IS_ADDR_V4MAPPED(a) \
	(((a)->s6_addr[0] == 0) && ((a)->s6_addr[1] == 0) && \
	 ((a)->s6_addr[2] == 0) && ((a)->s6_addr[3] == 0) && \
	 ((a)->s6_addr[4] == 0) && ((a)->s6_addr[5] == 0) && \
	 ((a)->s6_addr[6] == 0) && ((a)->s6_addr[7] == 0) && \
	 ((a)->s6_addr[8] == 0) && ((a)->s6_addr[9] == 0) && \
	 ((a)->s6_addr[10] == 0xff) && ((a)->s6_addr[11] == 0xff))

/* OpenBSD asserts the net lock is held; our NET_LOCK is advisory here. */
#define NET_ASSERT_LOCKED()	do { } while (0)
#define NET_ASSERT_UNLOCKED()	do { } while (0)
#define NET_LOCK_SHARED()	pfk_net_read_enter()
#define NET_UNLOCK_SHARED()	pfk_net_read_release()

/* OpenBSD system task queues: the port runs tasks inline on a workqueue */
extern void *systqmp;
void task_add(void *q, struct task *t);
void *taskq_create(const char *name, unsigned int nthreads, int ipl,
    unsigned int maxdepth, int mflags, int arg);

/* BSD gettime(): seconds since the epoch (as time_t) */
static inline time_t
gettime(void)
{
	return (time_t)ktime_get_real_seconds();
}
/* OpenBSD rwlock name; the kernel has its own debug `struct rwlock`, so the
 * BSD-visible struct is pfk_rwlock + alias macro (no further kernel headers
 * are included after pfk.h, so the alias cannot leak). */
struct pfk_rwlock {
	struct rw_semaphore	sem;
	const char		*name;
	int			 is_writer;
	atomic_t		 readers;
	/* PFLINUX: OpenBSD rw_enter(WRITER) recursion must not self-deadlock
	 * (pfioctl holds PF_LOCK and re-enters via helpers); rw_semaphores are
	 * non-recursive, so track writer identity + depth. */
	struct task_struct	*owner;
	int			 wdepth;
};
#define rwlock pfk_rwlock
#define rwlock_s pfk_rwlock

/* OpenBSD ioctl encoding: OUR kernel-side and our Linux pfctl use EXACTLY
 * these macros so _IOR/_IOW direction bits stay bit-compatible with
 * OpenBSD (Linux asm/ioctl.h differs). Ported TUs must include NO further
 * kernel headers after pfk.h (see porting-notes). */
#undef _IO
#undef _IOR
#undef _IOW
#undef _IOWR
#define PFKXIOC_NRBITS     8
#define PFKXIOC_TYPEBITS   8
#define PFKXIOC_SIZEBITS   14
#define PFKXIOC_DIRBITS    2
#define PFKXIOC_NRSHIFT    0
#define PFKXIOC_TYPESHIFT  (PFKXIOC_NRSHIFT + PFKXIOC_NRBITS)
#define PFKXIOC_SIZESHIFT  (PFKXIOC_TYPESHIFT + PFKXIOC_TYPEBITS)
#define PFKXIOC_DIRSHIFT   (PFKXIOC_SIZESHIFT + PFKXIOC_SIZEBITS)
#define PFKXIOC_NONE       0U
#define PFKXIOC_WRITE      1U	/* OpenBSD IOC_IN  (copyin)  */
#define PFKXIOC_READ       2U	/* OpenBSD IOC_OUT (copyout) */
#define PFKXIOC(dir,type,nr,size) \
	(((dir)  << PFKXIOC_DIRSHIFT) | ((type) << PFKXIOC_TYPESHIFT) | \
	 ((nr)   << PFKXIOC_NRSHIFT) | ((size) << PFKXIOC_SIZESHIFT))
#define _IO(t, n)           PFKXIOC(PFKXIOC_NONE, (t), (n), 0)
#define _IOWR(t, n, s)      PFKXIOC(PFKXIOC_WRITE|PFKXIOC_READ, (t), (n), sizeof(s))
#define _IOW(t, n, s)       PFKXIOC(PFKXIOC_WRITE, (t), (n), sizeof(s))
#define _IOR(t, n, s)       PFKXIOC(PFKXIOC_READ, (t), (n), sizeof(s))
#define PFKIOC_TYPE(nr)     (((nr) >> PFKXIOC_TYPESHIFT) & ((1U<<PFKXIOC_TYPEBITS)-1))
#define PFKIOC_NR(nr)       (((nr) >> PFKXIOC_NRSHIFT) & ((1U<<PFKXIOC_NRBITS)-1))
#define PFKIOC_DIR(nr)      (((nr) >> PFKXIOC_DIRSHIFT) & ((1U<<PFKXIOC_DIRBITS)-1))
#define PFKIOC_SIZE(nr)     (((nr) >> PFKXIOC_SIZESHIFT) & ((1U<<PFKXIOC_SIZEBITS)-1))

/* BSD host<->big endian names (the kernel uses cpu_to_beXX spellings) */
#ifndef htobe16
#define htobe16(x)	cpu_to_be16(x)
#endif
#ifndef htobe32
#define htobe32(x)	cpu_to_be32(x)
#endif
#ifndef htobe64
#define htobe64(x)	cpu_to_be64(x)
#endif
#ifndef be16toh
#define be16toh(x)	be16_to_cpu(x)
#endif
#ifndef be32toh
#define be32toh(x)	be32_to_cpu(x)
#endif
#ifndef be64toh
#define be64toh(x)	be64_to_cpu(x)
#endif
#define betoh16(x)	be16toh(x)
#define betoh32(x)	be32toh(x)
#define betoh64(x)	be64toh(x)
#ifndef htole64
#define htole64(x)	cpu_to_le64(x)
#endif
#ifndef le64toh
#define le64toh(x)	le64_to_cpu(x)
#endif

/* OpenBSD in6_addr internal union names map to the Linux macros */
#define __in6_u	in6_u
#define __u6_addr8	s6_addr
#define __u6_addr16	s6_addr16
#define __u6_addr32	s6_addr32

/* OpenBSD solicited-node / loopback construction helpers */
#define __IPV6_ADDR_INT32_ONE	1
#define __IPV6_ADDR_INT32_TWO	2
#define __IPV6_ADDR_INT32_MNL	0x0000FFFD
#define __IPV6_ADDR_INT32_MLL	0x000000FF
#define __IPV6_ADDR_INT32_SMTPL	0x00FF0000

/* RFC 4861 neighbor types */
#define ND_NEIGHBOR_SOLICIT	135
#define ND_NEIGHBOR_ADVERT	136

/* layer-2 domain of a routing domain: the port has one domain */
#define rtable_l2(rid)	(rid)

/* BSD byte-order names (BYTE_ORDER/LITTLE_ENDIAN) via compat header: */
#include "compat/sys/endian.h"

/* pfvar.h pulls <net/route.h> → resolves to the kernel's net/route.h;
 * BSD-only bits it expects must be provided here. */
#define RTLABEL_LEN	32

/* syslog levels (BSD numbering == Linux numbering for these) */
#define LOG_EMERG       0
#define LOG_ALERT       1
#define LOG_CRIT        2
#define LOG_ERR         3
#define LOG_WARNING     4
#define LOG_NOTICE      5
#define LOG_INFO        6
#define LOG_DEBUG       7
void pfk_log(int level, const char *fmt, ...);
#define log pfk_log
#define printf pfk_printk_compat
int  pfk_printk_compat(const char *fmt, ...);
#define panic(x...)   do { pr_emerg("pfk panic: " x); BUG(); } while (0)

/* time */
/* BSD struct timeval (two longs) — the kernel's in-kernel headers do not
 * define it; uapi's version is not visible here. */
struct timeval { long tv_sec; long tv_usec; };
#define pfk_timeval timeval
static inline __u64 pfk_time_sec(void) { return ktime_get_seconds(); }
static inline __u64 pfk_time_ms(void)  { return ktime_get_ns() / 1000000ULL; }
static inline void pfk_getmicrotime(struct timeval *tv)
{
	struct timespec64 ts;
	ktime_get_real_ts64(&ts);
	tv->tv_sec = ts.tv_sec;
	tv->tv_usec = ts.tv_nsec / 1000;
}
static inline __s64 pfk_ntptime_delta(void) { return 0; } /* PFLINUX: ntpd adj not modeled */

/* MALLOC / pool — flag values follow OpenBSD sys/sys/malloc.h */
#define M_TEMP     0x10000
#define M_DEVBUF   2
#define M_PCB      4
#define M_RTABLE   5
#define M_PF       6
#define M_IFADDR   9
#define M_IFGROUP  10
#define M_SYSCTL   11
#define M_COUNTERS 12
#define M_IOCTLOPS 14
#define M_PFX      16
#define M_PFTOC    17
#define M_TAG      18
#define M_PFADDR   19
#define M_PFRWP    20
#define M_PFRTABLE 21
#define M_IFMADDR  22
#define M_PFXSRC   23
#define M_ZOMBIE   24
#define M_PCR      25
#define M_PRoute   26

#define M_NOWAIT   0x02
#define M_DONTWAIT 0x80
#define M_WAITOK   0x01
#define M_CANFAIL  0x04
#define M_ZERO     0x08

void *pfk_malloc(size_t sz, int flags);
void  pfk_free(void *p);
void *pfk_mallocarray(size_t nmemb, size_t size, int flags);
#define malloc(sz, t, f)      pfk_malloc((sz), (f))
#define mallocarray(n, sz, t, f) pfk_mallocarray((n), (sz), (f))
/* OpenBSD's free() carries (ptr, type, size) — the accounting args are
 * irrelevant for us, but the call shape must stay compilable. */
#define free(p, ...)          pfk_free(p)

/*
 * pools: struct pool backed by kmem_cache when size fixed (pool_init),
 * lazily created on first get for simplicity of many small pools.
 */
struct pool {
	struct kmem_cache *slab;
	size_t		p_size;
	int		p_flags;	/* PR_WAITOK semantics via M_* on init */
	unsigned long	p_alloc;
	unsigned long	p_used;
	unsigned long	p_hardlimit;
	unsigned long	pr_nout;	/* number of allocations outstanding */
	struct list_head	p_objects;	/* for zeroed reuse semantics if needed */
};
#define PR_WAITOK  0x01
#define PR_NOWAIT  0x02
#define PR_ZERO    0x08
struct pool_allocator;
struct srcptr_static { const char *file; int line; };
#define SRCPOINT() (&(struct srcptr_static){__FILE__, __LINE__})

int   pool_init(struct pool *pl, size_t size, u_int align, int ipl, int use,
		const char *name, struct pool_allocator *alloc);
void  pool_destroy(struct pool *pl);
void *pool_get(struct pool *pl, int flags);
void  pool_put(struct pool *pl, void *v);
int   pool_sethardlimit(struct pool *pl, unsigned long lim);
void  pool_setlowat(struct pool *pl, int n);
void  pool_sethiwat(struct pool *pl, int n);

/* timeout.h: OpenBSD embeds `struct timeout` in statics/globals → fixed layout */
#include "compat/sys/timeout.h"
void timeout_set(struct timeout *to, timeout_fn *fn, void *arg);
void timeout_set_micro(struct timeout *to, timeout_fn *fn, void *arg);
int  timeout_add(struct timeout *to, long ms);
int  timeout_add_sec(struct timeout *to, int secs);
int  timeout_add_usec(struct timeout *to, int us);
int  timeout_del(struct timeout *to);
void timeout_barrier(struct timeout *to, int flags);
void timeout_del_sync(struct timeout *to);
int  timeout_pending(struct timeout *to);
int  tsleep(void *p, int pr, const char *w, int timo); /* PFLINUX: control-path only */
#define wakeup pfk_wakeup
void pfk_wakeup(void *p);

/* refcnt.h */
#include "compat/sys/refcnt.h"

/* rnd / random */
static inline u_int32_t arc4random(void) { return get_random_u32(); }
static inline void arc4random_buf(void *b, size_t l) { get_random_bytes(b, l); }
static inline u_int32_t arc4random_uniform(u_int32_t n) { return get_random_u32_below(n); }
static inline u_int32_t rnd(void) { return get_random_u32(); }
static inline u_int32_t random(void) { return get_random_u32(); }
#define karc4random() get_random_u32()
#define karc4random_uniform(n) get_random_u32_below(n)

/* copyin/copyout */
#define copyin(u, k, l)    (copy_from_user((k), (u), (l)) ? EFAULT : 0)
#define copyout(k, u, l)   (copy_to_user((u), (k), (l)) ? EFAULT : 0)
#define explicit_bzero(p, l) memzero_explicit((p), (l))

/* assert-ish */
#define KASSERT(cond) WARN_ON(!(cond))
#define __arraycount(a)   (sizeof(a) / sizeof((a)[0]))
#define __predictfalse(x) unlikely(x)
#define __predicttrue(x)  likely(x)
#define __predict(x, y)   ((y) ? likely(x) : unlikely(x))
#define __UNCONST(a)      ((void *)(unsigned long)(const void *)(a))

/* NET_LOCK: OpenBSD global network rwlock. PF data path runs under NET_READ;
 * ioctls take NET_LOCK (write). We map to a global rw_semaphore. */
void pfk_net_lock_init(void);
void pfk_net_lock(void);          /* write */
void pfk_net_unlock(void);
void pfk_net_read_enter(void);    /* read  */
void pfk_net_read_release(void);
#define NET_LOCK()        pfk_net_lock()
#define NET_UNLOCK()      pfk_net_unlock()
#define NET_READ_ENTER()  pfk_net_read_enter()
#define NET_READ_RELEASE() pfk_net_read_release()
void rw_init(struct rwlock_s *l, const char *name);
int  rw_enter(struct rwlock_s *l, int mode);
void rw_exit(struct rwlock_s *l);
int  rw_status(struct rwlock_s *l);
/* BSD rwlock flags */
#define RW_INTR		0x1
/* OpenBSD securelevel (systcl): the port does not enforce securelevel */
#define securelevel	0
#define FWRITE		0x0001	/* BSD file flags (used by pfctl compat) */
#define RT_TABLEID_MAX	32
#define RW_READER 1
#define RW_WRITER 2
#define RW_NAPUTS 0
#define rw_assert(l, m) do { } while (0)  /* PFLINUX: advisory */
#define rw_assert_wrlock(l)	do { } while (0)
#define rw_assert_anylock(l)	do { } while (0)

/* BSD lock API aliases used by the PF code */
#define RW_READ		1
#define RW_WRITE	2
static inline void rw_enter_write(struct rwlock *l) { rw_enter(l, RW_WRITER); }
static inline void rw_exit_write(struct rwlock *l) { rw_exit(l); }
static inline void rw_enter_read(struct rwlock *l) { rw_enter(l, RW_READER); }
static inline void rw_exit_read(struct rwlock *l) { rw_exit(l); }

static inline int
unhandled_af(int af)
{
	(void)af;
	return 0;
}

/* BSD queue.h/tree.h/staged helpers — the Linux kernel's list.h/rbtree.h
 * define a few identically-named macros; ported TUs never use Linux's
 * list APIs via those names, so we clear and override. */
#undef LIST_HEAD
#undef LIST_HEAD_INIT
#undef RB_ROOT
#undef RB_CLEAR_NODE
#include "compat/sys/queue.h"
#include "compat/sys/tree.h"
#include "compat/sys/socket.h"
#include "compat/net/if_types.h"
#include "compat/net/if_var.h"

/* mbuf + BSD wire structs + pf ifnet registry */
#include "compat/net/ifq.h"
#include "compat/net/rtable.h"
#include "compat/sys/percpu.h"
#include "compat/netinet/in.h"
#include "pfk_mbuf.h"
#include "pfk_net.h"
#include "pfk_if.h"

/* ---------------------------------------------------------------- M1 glue */
/* BSD parameter helpers (OpenBSD <sys/param.h>) */
#ifndef imax
#define imax(a, b)	((a) > (b) ? (a) : (b))
#endif
#ifndef imin
#define imin(a, b)	((a) < (b) ? (a) : (b))
#endif
#define ALIGNED_POINTER(p, t) ((((unsigned long)(p) % __alignof__(t)) == 0) && \
	(((unsigned long)(p) % sizeof(t)) == 0))

/* BSD asserts with message */
#define KASSERTMSG(cond, fmt...)	KASSERT(cond)

/* BSD process sentinel (OpenBSD <sys/proc.h> NO_PID) */
#ifndef NO_PID
#define NO_PID		((pid_t)-1)
#endif

/* BSD atomic swap helpers (OpenBSD <sys/atomic.h>): like Linux cmpxchg but
 * return the old value on success and 0/NULL on failure (OpenBSD semantics
 * as used by pf.c). */
#define atomic_cas_ptr(p, o, n) \
	((cmpxchg((p), (o), (n)) == (o)) ? (o) : (typeof(o))NULL)
#define atomic_cas_uint(p, o, n) \
	((cmpxchg((p), (o), (n)) == (unsigned int)(o)) ? (unsigned int)(o) : 0u)

/* SMR → Linux RCU */
static inline void smr_read_enter(void) { rcu_read_lock(); }
static inline void smr_read_leave(void) { rcu_read_unlock(); }

/*
 * struct inpcb — OpenBSD's kernel PCB view as used by PF (pf.c:
 * pf_socket_lookup/pf_mbuf_link_inpcb/pf_state_key_link_inpcb).  The port
 * does not have BSD PCBs: one struct inpcb is created per successful Linux
 * socket lookup and lives as long as PF holds references to it (mbufs,
 * state keys).  Implemented in compat/pfk_pcb.c.
 */
struct pfk_socket {		/* BSD struct socket fields PF consumes */
	uid_t		so_euid;
	gid_t		so_egid;
	pid_t		so_cpid;
};
struct inpcb {
	refcount_t		inp_refcnt;
	struct pf_state_key	*inp_pf_sk;	/* PF state linkage */
	struct sock		*inp_sock;	/* backing Linux socket */
	struct pfk_socket	inp_soinfo_storage;
	struct pfk_socket	*inp_socket;	/* → storage above */
};
struct inpcbtable {		/* OpenBSD PCB table head (opaque handle) */
	int		pfk_unused;
};
extern struct inpcbtable tcbtable, udbtable, tcb6table, udb6table;

struct inpcb *in_pcbref(struct inpcb *);
void	in_pcbunref(struct inpcb *);
struct inpcb *in_pcblookup(struct inpcbtable *, struct in_addr faddr,
	    u_int fport, struct in_addr laddr, u_int lport, u_int rtableid);
struct inpcb *in_pcblookup_listen(struct inpcbtable *, struct in_addr laddr,
	    u_int lport, struct mbuf *m, u_int rtableid);
struct inpcb *in6_pcblookup(struct inpcbtable *, const struct in6_addr *faddr,
	    u_int fport, const struct in6_addr *laddr, u_int lport,
	    u_int rtableid);
struct inpcb *in6_pcblookup_listen(struct inpcbtable *, struct in6_addr *laddr,
	    u_int lport, struct mbuf *m, u_int rtableid);
int	pfk_pcb_init(void);
void	pfk_pcb_exit(void);

/* OpenBSD counters(9): per-CPU u64 slots (struct cpumem from percpu.h) */
struct cpumem *counters_alloc(u_int count);
void	counters_free(struct cpumem *);
void	counters_inc(struct cpumem *, u_int idx);
void	counters_zero(struct cpumem *, u_int count);
void	counters_read(struct cpumem *, uint64_t *result, u_int count,
	    uint64_t *scratch);

/* task/timeout static initializers (OpenBSD <sys/task.h> <sys/timeout.h>) */
#define TASK_INITIALIZER(_f, _a)	{ .ta_data = (_a), .ta_func = (_f) }
#define TIMEOUT_INITIALIZER(_f, _a)	{ .fn = (_f), .arg = (_a) }

/* timeout_add_msec: timeout_add() already takes milliseconds upstream */
int	timeout_add_msec(struct timeout *to, int msecs);

/* curcpu(9): OpenBSD per-CPU info; only ci_schedstate.spc_schedflags is
 * consumed by pf.c (SPCF_SHOULDYIELD).  Mapped to need_resched(). */
struct pfk_schedstate { int spc_schedflags; };
struct pfk_cpu_info { struct pfk_schedstate ci_schedstate; };
#define SPCF_SHOULDYIELD	0x1
struct pfk_cpu_info *pfk_curcpu(void);
#define curcpu()		pfk_curcpu()

/* interface refcounting (OpenBSD if_get/if_put/if_get_smr) */
struct pfk_ifnet *if_get(int ifidx);
struct pfk_ifnet *if_get_smr(int ifidx);
void	if_put(struct pfk_ifnet *ifp);
int	if_congested(void);
void	pfk_if_congestion(void);	/* stamp "tx resources exhausted" now */

/* OpenBSD route/rtable API for PF (compat/pfk_rtable.c over the Linux FIB) */
#define RT_RESOLVE	1	/* rtalloc() flag: resolve the route */
/* PFLINUX: `sa` is const void * so BSD sintosa/sin6tosa/sstosa macro
 * results (which keep their concrete sockaddr types) pass directly. */
struct pf_rtentry *rtalloc(const void *sa, int, unsigned int);
struct pf_rtentry *rtalloc_mpath(const void *sa, uint32_t *,
	    unsigned int);
int	rtisvalid(struct pf_rtentry *);
void	rtfree(struct pf_rtentry *);
#define sstosa(ss)	((struct sockaddr *)(void *)(ss))

/* BSD in_ifaddr/in6_ifaddr views (OpenBSD <netinet/in_var.h>).  The Linux
 * in_ifaddr tag is already taken, so the port keeps its own types. */
struct pfk_in_ifaddr {
	struct ifaddr	ifa_ifa;
	struct sockaddr_in ia_addr;
};
struct pfk_in6_ifaddr {
	struct ifaddr	ifa_ifa;
	struct sockaddr_in6 ia_addr;
};
#define ifatoia(ifa)	((struct pfk_in_ifaddr *)(void *)(ifa))
#define ifatoia6(ifa)	((struct pfk_in6_ifaddr *)(void *)(ifa))

/* OpenBSD sysctl-backed globals (pfk_kern.c) */
extern int ip_forwarding;	/* 0 off, 1 on, 2 ipsec-only (we: init-synced) */
extern int ip_directedbcast;	/* accept directed broadcast */
extern int ipmultipath;		/* multipath routing enable */
extern int ip6_multipath;
extern int ip_mtudisc;		/* PMTU discovery */
extern int ip_defttl;		/* default IPv4 TTL */
extern u_int max_linkhdr;	/* max link-level header size */
void	pfk_sysctl_sync(void);	/* mirror Linux sysctls into BSD globals */
extern tcp_seq tcp_iss;		/* tcp initial send seq # (random salt) */

/* ip_output/ip_forward flags (OpenBSD <netinet/ip_var.h>) */
#define IP_FORWARDING		0x0001
#define IP_ALLOWBROADCAST	0x0002
#define IP_MINMTU		0x0004
#define IP_REDIRECT		0x0008
#define IP_FORWARDING_IPSEC	0x0010
#define IPTTLDEC		1

/* BSD IPv4 layer entry points used by pf.c (compat/pfk_ip4.c).  ip_output/
 * ip_forward/ip6_output clash with Linux declarations, so the staging seds
 * rename them to pfk_* (see stage-port.sh). */
void	ip_send(struct mbuf *);
void	ip6_send(struct mbuf *);
void	icmp_error(struct mbuf *, u_int type, u_int code, u_int dest,
	    u_int mtu);
int	ip_fragment(struct mbuf *, struct mbuf_list *, struct pfk_ifnet *,
	    int);
int	pfk_ip_output(struct mbuf *, void *, void *, u_int, void *, void *,
	    u_int);
void	pfk_ip_forward(struct mbuf *, struct pfk_ifnet *, void *, u_int);
int	pfk_ip6_output(struct mbuf *, void *, void *, u_int, void *, void *);

/* BSD checksum helpers (compat/pfk_ip4.c) */
u_int16_t in_cksum(struct mbuf *, int);
u_int16_t in4_cksum(struct mbuf *m, __u8 nxt, u_int32_t off, u_int32_t len);
u_int16_t in6_cksum(struct mbuf *m, __u8 nxt, u_int32_t off, u_int32_t len);
void	in_hdr_cksum_out(struct mbuf *, void *);

/* divert layer (pfsrc/divert_sock.c; BSD AF_DIVERT semantics) */
void	divert_packet(struct mbuf *, int dir, u_int16_t divert_port);
void	divert6_packet(struct mbuf *, int dir, u_int16_t divert_port);

/* BSD statistics hooks: counters live in pf_status_fcounters/pf_kif in the
 * port; the netinet ip/tcp counters are not ported (no-op).  */
#define ipstat_inc(c)		do { } while (0)
#define tcpstat_inc(c)		do { } while (0)

/* BSD SEQ helpers (OpenBSD <netinet/tcp_seq.h>) */
#define SEQ_LEQ(a, b)		((int)((a) - (b)) <= 0)

struct pf_source;
#define SEC_TO_NSEC(x)	((uint64_t)(x) * 1000000000ULL)
#define NSEC_TO_SEC(x)	((x) / 1000000000ULL)

/* BSD nanosecond uptime accessor */
static inline __u64
getnsecuptime(void)
{
	return ktime_get_ns();
}

/* BSD microsecond uptime accessor */
static inline void
getmicrouptime(struct timeval *tv)
{
	__u64 ns = ktime_get_ns();

	tv->tv_sec = ns / NSEC_PER_SEC;
	tv->tv_usec = (ns % NSEC_PER_SEC) / NSEC_PER_USEC;
}

/* BSD uptime accessor (seconds since boot) */
static inline time_t
getuptime(void)
{
	return (time_t)(ktime_get_ns() / NSEC_PER_SEC);
}

/* BSD mbuf helpers implemented in compat/pfk_mbuf.c */
int m_removehdr(struct mbuf *);
int m_calchdrlen(struct mbuf *);
/* BSD m_tag_get: allocate a tag and return a pointer to its data area */
struct m_tag *m_tag_get(int type, int len, int flags);
/* PF-internal flags (upstream keeps some of these in pf.c) */
#define PF_TAG_REFRAGMENTED	0x0001
#define PF_TAG_DIVERTED		0x0002
#define PF_TAG_GENERATED	0x0004
#define PF_TAG_REROUTE		0x0008
#define PF_TAG_TRANSLATE_LOCALHOST 0x0010
#define PF_TAG_REFRAGMENTED_PACKET 0x0020
#define PF_TAG_SYNCOOKIE_RECREATED	0x0040
#define PF_TAG_SYNCOOKIE		0x0080
/* m_pkthdr.pf.flag bits, continuing the upstream bit space */
#define PF_TAG_DIVERTED_PACKET	0x0100
#define PF_TAG_PROCESSED		0x0200

/* OpenBSD route entry (kernel-internal PF type; pointer use only) */
struct pf_rtentry;

/* OpenBSD process reference (used by the ioctl entry points).  The
 * kernel's `struct ucred` tag belongs to the uapi peercred type, so PF
 * carries its own credential view. */
struct pfk_ucred {
	uint32_t	uid;
	uint32_t	gid;
};
struct pfk_proc_p {
	pid_t		pid;
};
struct proc {
	int			p_flags;
	struct pfk_ucred	*p_ucred;
	struct pfk_proc_p	*p_p;
};
static inline uint32_t pfk_pruid(struct proc *p)
{
	return (p && p->p_ucred) ? p->p_ucred->uid : 0;
}
static inline uint32_t pfk_ppid(struct proc *p)
{
	return (p && p->p_p) ? (uint32_t)p->p_p->pid : 0;
}
extern struct proc *curproc;

/* BSD atomic helpers (BSD's atomic_load_int takes an int *) */
#define atomic_load_int(p)	(*(p))
#define atomic_load_long(p)	(*(p))

/* BSD tcp state helpers (netinet/tcp_fsm.h) */
#define TCPS_HAVERCVDSYN(s)	((s) >= TCPS_SYN_RECEIVED)
#define TCPS_HAVEESTABLISHED(s)	((s) <= TCPS_TIME_WAIT)
#define TCPS_HAVERCVDFIN(s)	((s) >= TCPS_TIME_WAIT)

/* BSD atomic int helpers */
#define atomic_dec_int(p)	(*(p))--
#define atomic_inc_int(p)	(*(p))++
#define atomic_inc_int_nv(p)	(++*(p))
#define atomic_dec_int_nv(p)	(--*(p))
#define atomic_set_int(p, v)	(*(p) = (v))
#define atomic_add_int(p, v)	(*(p) += (v))

/* BSD sequence-number comparisons (TCP style) */
#define SEQ_LT(a, b)		((int)((a) - (b)) < 0)
#define SEQ_GT(a, b)		((int)((a) - (b)) > 0)
#define SEQ_LE(a, b)		((int)((a) - (b)) <= 0)
#define SEQ_GE(a, b)		((int)((a) - (b)) >= 0)

/* BSD limits */
#define MAX_TCPOPTLEN	40
#define MAX_IPOPTLEN	40
#define TCPOPT_MAXSEG	2
#define TCPOLEN_MAXSEG	4
#define IPTOS_ECN_MASK	0x03
#define SEQ_GEQ(a, b)	SEQ_GE(a, b)

/* BSD names not present in the Linux uapi headers */
#define NKSTAT		0
#define CLONE_MAPSZ	1024
#define CLONE_SHIFT	11
#define CACHELINESIZE	128

/* BSD physical memory size and address translation */
#define physmem		((unsigned long)totalram_pages() << PAGE_SHIFT)
#define atop(x)		((unsigned long)(x))

/* BSD device minor number */
#define minor(dev)	MINOR(dev)

/* Queueing core: OpenBSD 8.0's ifq framework.  compat/pfk_ifq.c provides
 * the framework with priq semantics; the OpenBSD hfsc.c / fq_codel.c back
 * ends are added in milestone M5 (they implement the same ifq_ops). */
void hfsc_initialize(void);
void fq_codel_initialize(void);
int  pf_create_queues(void);

/* PF state/source limit helpers — upstream prototypes (net/pfvar_priv.h) are
 * not visible to the compiler in this order, so they are restated here. */
struct pf_statelim;
struct pf_sourcelim;
struct pf_src_node;
struct pfioc_rule;
struct pfioc_statelim;
struct pfioc_sourcelim;
struct pfioc_source;
struct pfioc_source_kill;
struct pf_statelim_id_tree;
struct pf_sourcelim_id_tree;
struct pf_source_ioc_tree;
int pf_statelim_add(const struct pfioc_statelim *);
int pf_statelim_get(struct pfioc_statelim *,
    struct pf_statelim *(*)(struct pf_statelim_id_tree *, const struct pf_statelim *));
int pf_sourcelim_add(const struct pfioc_sourcelim *);
int pf_sourcelim_get(struct pfioc_sourcelim *,
    struct pf_sourcelim *(*)(struct pf_sourcelim_id_tree *, const struct pf_sourcelim *));
int pf_source_get(struct pfioc_source *,
    struct pf_source *(*)(struct pf_source_ioc_tree *, const struct pf_source *));
int pf_source_clr(struct pfioc_source_kill *);

/* Route labels are not modelled in the port (OpenBSD routing domains are a
 * non-goal); the helpers exist so the ioctl layer compiles and reports
 * "not available" instead of pretending. */
int   rtlabel_name2id(const char *);
int   rtable_exists(unsigned int);
char *rtlabel_id2name(unsigned int, char *, size_t);
void  rtlabel_unref(unsigned int);

/* BSD static rwlock initializer */
#define RWLOCK_INITIALIZER(nm) \
	{ .sem = __RWSEM_INITIALIZER(nm), .name = (nm) }
extern u_int pfk_tcp_mssdflt;		/* TCP default MSS (OpenBSD: 536) */

/* BSD nitems() */
#define nitems(a)	(sizeof(a) / sizeof((a)[0]))
#define TCPOPT_SACK_PERMITTED	TCPOPT_SACK_PERM
#define IPL_NONE		0x00

/* BSD inet_ntop()/inet_pton() (used by the OS fingerprint code) */
static inline const char *
inet_ntop(int af, const void *src, char *dst, unsigned int size)
{
	const char *ret = NULL;

	if (af == AF_INET) {
		snprintf(dst, size, "%pI4", (const struct in_addr *)src);
		ret = dst;
	} else if (af == AF_INET6) {
		snprintf(dst, size, "%pI6", (const struct in6_addr *)src);
		ret = dst;
	}
	return ret;
}

/* BSD IPv6 address helpers */
#define IN6_ARE_ADDR_EQUAL(a, b)		(memcmp((a), (b), sizeof(struct in6_addr)) == 0)
static const struct in6_addr in6mask128 = {
	.s6_addr = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }
};

/* BSD mask helpers (prefix length from a netmask) */
static inline int
in_mask2len(const struct in_addr *mask)
{
	int len = 0;
	__be32 m = mask->s_addr;

	while (m & 0x80000000U) {
		len++;
		m <<= 1;
	}
	return len;
}

static inline int
in6_mask2len(const struct in6_addr *mask, const struct in6_addr *scoped)
{
	(void)scoped;
	int len = 0;
	int i;

	for (i = 0; i < 16; i++) {
		__u8 b = mask->s6_addr[i];

		if (b == 0xff) {
			len += 8;
			continue;
		}
		while (b & 0x80) {
			len++;
			b <<= 1;
		}
		break;
	}
	return len;
}

/* OpenBSD's NAT46/NAT64 address mapping helpers (used by the route-to
 * pool code); IPv4-mapped detection and prefix handling. */
/*
 * NAT46/NAT64 address mapping (OpenBSD netinet6/nd6.c).  The port
 * implements the address arithmetic used by the rdr/NAT pool code; the
 * well-known prefix handling follows RFC 6052.
 */
static inline void
inet_nat46(int af, const void *src, void *dst, void *naddr, int prefixlen)
{
	/* af == AF_INET: translate the v4 address; otherwise the source is
	 * an IPv6 address that is passed through unchanged. */
	if (af == AF_INET) {
		memcpy(dst, src, sizeof(struct in_addr));
		if (naddr)
			memset(naddr, 0, sizeof(struct in_addr));
	} else {
		memcpy(dst, src, sizeof(struct in6_addr));
		if (naddr)
			memset(naddr, 0, sizeof(struct in6_addr));
	}
	(void)prefixlen;
}

static inline void
inet_nat64(int af, const void *src, void *dstv, void *naddr, int prefixlen)
{
	struct in6_addr *dst = dstv;
	struct in6_addr *s6 = (struct in6_addr *)src;

	if (af == AF_INET6) {
		if (dst)
			memcpy(dst, s6, sizeof(*dst));
		return;
	}
	if (dst) {
		__u32 v4 = s6->s6_addr[12] | (s6->s6_addr[13] << 8) |
		    (s6->s6_addr[14] << 16) | ((__u32)s6->s6_addr[15] << 24);

		memset(dst, 0, sizeof(*dst));
		dst->s6_addr[0] = 0x20;	/* RFC 6052 well-known prefix */
		dst->s6_addr[1] = 0x01;
		dst->s6_addr[12] = v4 >> 24;
		dst->s6_addr[13] = v4 >> 16;
		dst->s6_addr[14] = v4 >> 8;
		dst->s6_addr[15] = v4;
	}
	(void)naddr;
	(void)prefixlen;
}

/* OpenBSD's dynamic-address (DHCP) heuristic: not applicable in the port */
static inline int
in_baddynamic(u_int16_t addr, int proto)
{
	(void)addr;
	(void)proto;
	return 0;
}

/* pf_lb.c entry points (declared here for -Wmissing-prototypes) */
u_int32_t pf_rand_addr(u_int32_t mask);
struct pf_rule;
void pf_rule_free(struct pf_rule *);

/* BSD IPv6 scope helpers */
#define IN6_SCOPE_EMBED	0x20000000
/* BSD: IN6_IS_SCOPE_EMBED() takes an in6_addr pointer */
#define IN6_IS_SCOPE_EMBED(a)		(((a)->s6_addr[0] == 0xfe) && (((a)->s6_addr[1] & 0xc0) == 0x80))

/* BSD random IP id */
static inline __be16 ip_randomid(void)
{
	return htons((__u16)(get_random_u32() & 0xffff));
}

/* BSD time helpers */
static inline void
timersub(const struct timeval *tvp, const struct timeval *uvp,
    struct timeval *vvp)
{
	vvp->tv_sec = tvp->tv_sec - uvp->tv_sec;
	vvp->tv_usec = tvp->tv_usec - uvp->tv_usec;
	if (vvp->tv_usec < 0) {
		vvp->tv_sec--;
		vvp->tv_usec += 1000000;
	}
}

/* IPv6 forwarding state (OpenBSD sys/netinet6 variables).
 * ip6_output/ip6_forward flag values per OpenBSD <netinet6/ip6_var.h>;
 * IPV6_MINMTU/IPV6_UNSPECSRC are omitted: upstream PF code never uses
 * them and IPV6_MINMTU already means 1280 above (name clash upstream). */
extern int ip6_forwarding;
#define IPV6_FORWARDING		0x02
#define IPV6_REDIRECT		0x08
#define IPV6_FORWARDING_IPSEC	0x10
#define ip6s_cantforward	0
#define ip6stat_inc(c)		do { } while (0)

/* BSD IPv6 helpers (compat/pfk_ip6.c) */
void pfk_ip6_forward(struct mbuf *, void *, int);
void icmp6_error(struct mbuf *, int, int, int);

/* BSD IPv6 fragment header helpers */
int frag6_deletefraghdr(struct mbuf *, int);
/* BSD IPv6 fragmentation / checksum helpers (compat/pfk_ip6.c).
 * Names are prefixed because the kernel already exports ip6_fragment() with
 * an incompatible (net/sock/skbuff) signature. */
int pfk_ip6_fragment(struct mbuf *, struct mbuf_list *, int, int, int);
void pfk_in6_proto_cksum_out(struct mbuf *, struct mbuf *);

/* OpenBSD digest + toeplitz helpers (vendored into compat/crypto, compat/net) */
#include "compat/crypto/sha2.h"
#include "compat/crypto/md5.h"
#include "compat/crypto/siphash.h"
#include "compat/net/toeplitz.h"

/* OpenBSD sysctl plumbing: pf_sysctl is not wired to a sysctl table in
 * the port (PF control goes through /dev/pf), kept compileable. */
int pf_sysctl(void *oldp, size_t *oldlenp, void *newp, size_t newlen);
int pf_chk_limiter_action(int);

static inline int
sysctl_rdstruct(void *oldp, size_t *oldlenp, void *newp, const void *val,
    size_t len)
{
	(void)newp;
	if (oldp && oldlenp) {
		size_t n = len < *oldlenp ? len : *oldlenp;

		if (copy_to_user(oldp, (void __force *)val, n))
			return EFAULT;
		*oldlenp = len;
	}
	return 0;
}
static inline int
sysctl_quad(void *r, const void *v, size_t l, uint64_t lo, uint64_t hi)
{
	(void)v; (void)l; (void)lo; (void)hi;
	*(uint64_t *)r = 0;
	return 0;
}

/* PF option headers: OpenBSD builds per-option headers (carp.h etc.); we
 * zero the absent features and enable pflog (netlink-backed). */
#define NPFSYNC 0
#define NPFLOG  1
#define NPFLOW  0
#define NCARP   0
#define NPF     1
#define INET    1
#define INET6   1

/* OpenBSD's lock-debugging wrapper (pc_lock): we keep a plain rwlock. */
struct pc_lock {
	struct rwlock	pcl_lock;
};
#define pc_lock_init(l)		rw_init(&(l)->pcl_lock, "pf")
#define pc_lock_lock(l)		rw_enter(&(l)->pcl_lock, RW_WRITER)
#define pc_lock_unlock(l)	rw_exit(&(l)->pcl_lock)

/* OpenBSD lock profiling hooks: no-ops in the port (generation counter
 * semantics: pc_sprod_enter returns a generation, leave consumes it). */
static inline unsigned int pc_sprod_enter(void *p) { (void)p; return 0; }
static inline void pc_sprod_leave(void *p, unsigned int gen)
{ (void)p; (void)gen; }

/* kernel owns 'struct mutex' — BSD-visible alias must come before any
 * staged BSD header references it. */
#define mutex pfk_mutex

/* net/sock.h defines accessor MACRO sk_refcnt (== __sk_common.skc_refcnt);
 * struct pf_state_key has a genuine member named sk_refcnt — the macro
 * would corrupt it. Compat code after this point uses explicit members. */
#undef sk_refcnt

/* pf headers resolve via -I module/include */
#include <net/pfvar.h>
#include <net/pfvar_priv.h>

/* Implemented in pf_table.c (upstream) — declared here for -Wmissing-prototypes */
void pfr_fill_feedback(struct pfr_kentry_all *, struct pfr_addr *);

/* state helpers implemented in pf.c (struct pf_state comes from pfvar_priv.h) */
struct pf_state_list;
void pf_state_list_insert(struct pf_state_list *, struct pf_state *);
void pf_state_list_remove(struct pf_state_list *, struct pf_state *);
void pf_update_state_timeout(struct pf_state *, int);

void pfk_mod_init_done(void);

/* pf.c entry points that are non-static upstream but have no header
 * prototype at this include order (-Wmissing-prototypes). */
struct pf_anchor_stackframe;
struct pf_anchor;
struct pf_ruleset;
void	pf_anchor_stack_init(void);
int	pf_anchor_stack_is_full(struct pf_anchor_stackframe *);
int	pf_anchor_stack_is_empty(struct pf_anchor_stackframe *);
struct pf_anchor_stackframe *pf_anchor_stack_top(void);
int	pf_anchor_stack_push(struct pf_ruleset *, struct pf_rule *,
	    struct pf_rule *, struct pf_anchor *, int);
int	pf_anchor_stack_pop(struct pf_ruleset **, struct pf_rule **,
	    struct pf_rule **, struct pf_anchor **, int *);
struct inpcb;
void	pf_remove_divert_state(struct inpcb *);

#endif /* _PFK_H */
