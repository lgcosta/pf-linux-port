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
#include <linux/slab.h>
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

/* OpenBSD rwlock name; the kernel has its own debug `struct rwlock`, so the
 * BSD-visible struct is pfk_rwlock + alias macro (no further kernel headers
 * are included after pfk.h, so the alias cannot leak). */
struct pfk_rwlock {
	struct rw_semaphore	sem;
	const char		*name;
	int			 is_writer;
	atomic_t		 readers;
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
#define PFKXIOC_SIZEBITS   13
#define PFKXIOC_DIRBITS    2
#define PFKXIOC_NRSHIFT    0
#define PFKXIOC_TYPESHIFT  (PFKXIOC_NRSHIFT + PFKXIOC_NRBITS)
#define PFKXIOC_SIZESHIFT  (PFKXIOC_TYPESHIFT + PFKXIOC_TYPEBITS)
#define PFKXIOC_DIRSHIFT   (PFKXIOC_SIZESHIFT + PFKXIOC_SIZEBITS + 1)
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

/* OpenBSD endian names htobe64/htole64/beXXXtoh and ntoh/hton are already
 * provided by linux/byteorder/generic.h — do NOT redefine (Werror).
 * BSD byte-order names (BYTE_ORDER/LITTLE_ENDIAN) via compat header: */
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
typedef long time_t;
struct timeval { long tv_sec; long tv_usec; };	/* BSD layout (uapi may clash) */
static inline __u64 pfk_time_sec(void) { return ktime_get_seconds(); }
static inline __u64 pfk_time_ms(void)  { return ktime_get_ns() / 1000000ULL; }
struct pfk_timeval { __s64 tv_sec; __s64 tv_usec; };
static inline void pfk_getmicrotime(struct pfk_timeval *tv)
{
	struct timespec64 ts;
	ktime_get_real_ts64(&ts);
	tv->tv_sec = ts.tv_sec;
	tv->tv_usec = ts.tv_nsec / 1000;
}
static inline __s64 pfk_ntptime_delta(void) { return 0; } /* PFLINUX: ntpd adj not modeled */

/* MALLOC / pool */
#define M_TEMP     0x10000
#define M_PF       0x10001
#define M_DEVBUF   0x10002
#define M_IFADDR   0x10003
#define M_PFX       0x10004
#define M_TAG        0x10005
#define M_PFADDR     0x10006
#define M_PFRWP      0x10007
#define M_PFTOC      0x10008
#define M_PFRTABLE   0x10009
#define M_IFMADDR    0x1000a
#define M_PFXSRC     0x1000b
#define M_ZOMBIE     0x1000c
#define M_PCR        0x1000d
#define M_PRoute     0x1000e

#define M_NOWAIT   0x01
#define M_WAITOK   0x02
#define M_CANFAIL  0x04
#define M_ZERO     0x08

void *pfk_malloc(size_t sz, int flags);
void  pfk_free(void *p);
#define malloc(sz, t, f)      pfk_malloc((sz), (f))
#define free(p, t)            pfk_free(p)

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
	struct list_head	p_objects;	/* for zeroed reuse semantics if needed */
};
#define PR_WAITOK  0x01
#define PR_NOWAIT  0x02
#define PR_ZERO    0x08
struct srcptr_static { const char *file; int line; };
#define SRCPOINT() (&(struct srcptr_static){__FILE__, __LINE__})

int   pool_init(struct pool *pl, size_t size, int align, int ipl,
		const char *use, void *p, int flags,
		void (*ih)(void *, void *), void *arg, struct srcptr_static *sp);
void  pool_destroy(struct pool *pl);
void *pool_get(struct pool *pl, int flags);
void  pool_put(struct pool *pl, void *v);
void  pool_sethardlimit(struct pool *pl, unsigned long lim, const char *msg, int msglevel);

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
void rw_enter(struct rwlock_s *l, int mode);
void rw_exit(struct rwlock_s *l);
int  rw_status(struct rwlock_s *l);
#define RW_READER 1
#define RW_WRITER 2
#define RW_NAPUTS 0
#define rw_assert(l, m) do { } while (0)  /* PFLINUX: advisory */

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
#include "pfk_mbuf.h"
#include "pfk_net.h"
#include "pfk_if.h"

/* OpenBSD digest + toeplitz helpers (vendored into compat/crypto, compat/net) */
#include "compat/crypto/sha2.h"
#include "compat/crypto/md5.h"
#include "compat/crypto/siphash.h"
#include "compat/net/toeplitz.h"

/* pf headers resolve via -I module/include */
#define NPFSYNC 0
#define NPFLOG  1
#define NPFLOW  0
#define NCARP   0
#define NPF     1

void pfk_mod_init_done(void);

#endif /* _PFK_H */
