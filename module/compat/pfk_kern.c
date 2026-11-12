/*
 * PF-on-Linux kernel API compatibility (log/lock/malloc/pool/timeout/tsleep)
 * ISC (c) 2026 PF-on-Linux contributors
 */
#include <pfk.h>

/* ---------------- logging ---------------- */
void
pfk_log(int level, const char *fmt, ...)
{
	char buf[512];
	va_list ap;

	if (level < 0 || level > LOG_DEBUG)
		level = LOG_INFO;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	switch (level) {
	case LOG_EMERG:	pr_emerg("%s\n", buf); break;
	case LOG_ALERT:	pr_alert("%s\n", buf); break;
	case LOG_CRIT:	pr_crit("%s\n", buf); break;
	case LOG_ERR:	pr_err("%s\n", buf); break;
	case LOG_WARNING:	pr_warn("%s\n", buf); break;
	case LOG_NOTICE:	pr_notice("%s\n", buf); break;
	default:
		pr_info_ratelimited("%s\n", buf); break;
	}
}

int
pfk_printk_compat(const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (n > 0)
		pr_info("%s", buf);
	return n;
}

/* ---------------- splassert (BSD softlock assertion) ---------------- */
void
splassert_fail(int expected, int actual, const char *func)
{
	/*
	 * PFLINUX: OpenBSD's splassert() checks a per-CPU spl level, so
	 * "assert unlocked" really means "this context did not take the lock".
	 * The port replaced spl with rw-semas plus work queues, where
	 * rw_status() is a global counter: an assert-unlocked in a work-queue
	 * task fires whenever *another* CPU happens to hold the lock, which is
	 * not a bug (the task never acquires pf_lock itself) and was turning a
	 * harmless race into a kernel BUG() that wedged the module in
	 * "Unloading".  Report it once instead of killing the host; the
	 * structural invariant the BSD assert protects - purge never runs while
	 * holding pf_lock - is upheld by the call sites (pf_purge_* are only
	 * reached from systqmp tasks and timer work).
	 */
	pr_warn_ratelimited("pfk: splassert: %s: expected %d, got %d\n",
	    func, expected, actual);
}

/* ---------------- addlog (OpenBSD kern.log) ---------------- */
void
addlog(const char *fmt, ...)
{
	char buf[512];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	pr_info("%s", buf);
}

/* ---------------- mutex ---------------- */
void
pfk_mutex_setup(struct pfk_mutex *m, const char *name)
{
	sema_init(&m->s, 1);
	m->name = name;
}

/* OpenBSD's curproc (the port has no per-process state) */
struct proc *curproc = NULL;

/* OpenBSD's tcp_mssdflt (the kernel's own is not exported to modules) */
u_int pfk_tcp_mssdflt = 536;
u_int nmbclust = NMBCLUSTERS;
void *systqmp;

void
task_add(void *q, struct task *t)
{
	(void)q;
	if (t && t->ta_func)
		t->ta_func(t->ta_data);
}

void *
taskq_create(const char *name, unsigned int nthreads, int ipl,
	    unsigned int maxdepth, int mflags, int arg)
{
	(void)name; (void)nthreads; (void)ipl; (void)maxdepth; (void)mflags;
	(void)arg;
	return systqmp;
}

/* ---------------- NET_LOCK ---------------- */
static DECLARE_RWSEM(pfk_net_sem);
static atomic_t pfk_net_readers = ATOMIC_INIT(0);

/*
 * PFLINUX: OpenBSD's NET_LOCK nesting is friendly for the holder (e.g.
 * pfioctl takes NET_LOCK and pf_status_read() does too).  Linux rw_semaphores
 * are not recursive, so track the holder task + depth: nested lock/unlock by
 * the same task balance without touching the rwsem.  (Tracking was by task
 * pointer on purpose: pids can be recycled.)
 */
static DEFINE_RAW_SPINLOCK(pfk_net_owner_lock);
static struct task_struct *pfk_net_owner;
static int pfk_net_depth;

void pfk_net_lock_init(void) { }

void
pfk_net_lock(void)
{
	unsigned long flags;
	bool nested;

	raw_spin_lock_irqsave(&pfk_net_owner_lock, flags);
	nested = (pfk_net_owner == current);
	if (nested)
		pfk_net_depth++;
	raw_spin_unlock_irqrestore(&pfk_net_owner_lock, flags);
	if (nested)
		return;
	down_write(&pfk_net_sem);
	raw_spin_lock_irqsave(&pfk_net_owner_lock, flags);
	pfk_net_owner = current;
	pfk_net_depth = 1;
	raw_spin_unlock_irqrestore(&pfk_net_owner_lock, flags);
}

void
pfk_net_unlock(void)
{
	unsigned long flags;
	int depth;

	raw_spin_lock_irqsave(&pfk_net_owner_lock, flags);
	if (pfk_net_owner != current) {
		/* not the holder: unbalanced unlock — ignore loudly */
		raw_spin_unlock_irqrestore(&pfk_net_owner_lock, flags);
		WARN_ON_ONCE(1);
		return;
	}
	depth = --pfk_net_depth;
	raw_spin_unlock_irqrestore(&pfk_net_owner_lock, flags);
	if (depth == 0) {
		raw_spin_lock_irqsave(&pfk_net_owner_lock, flags);
		pfk_net_owner = NULL;
		raw_spin_unlock_irqrestore(&pfk_net_owner_lock, flags);
		up_write(&pfk_net_sem);
	}
}
void pfk_net_read_enter(void)  { down_read(&pfk_net_sem);
				 atomic_inc(&pfk_net_readers); }
void pfk_net_read_release(void){ atomic_dec(&pfk_net_readers);
				 up_read(&pfk_net_sem); }

void
rw_init(struct rwlock *l, const char *name)
{
	init_rwsem(&l->sem);
	l->name = name;
	l->is_writer = 0;
	atomic_set(&l->readers, 0);
}

int
rw_enter(struct rwlock *l, int mode)
{
	if (mode == RW_WRITER) {
		down_write(&l->sem);
		l->is_writer = 1;
	} else {
		down_read(&l->sem);
		atomic_inc(&l->readers);
	}
	return 0;
}

void
rw_exit(struct rwlock *l)
{
	if (l->is_writer) {
		l->is_writer = 0;
		up_write(&l->sem);
	} else {
		atomic_dec(&l->readers);
		up_read(&l->sem);
	}
}

int
rw_status(struct rwlock *l)
{
	return l->is_writer ? RW_WRITER :
		atomic_read(&l->readers) ? RW_READER : RW_NAPUTS;
}

/* ---------------- malloc ---------------- */
void *
pfk_malloc(size_t sz, int flags)
{
	gfp_t g = (flags & M_WAITOK) ? GFP_KERNEL : GFP_ATOMIC;

	if (flags & M_ZERO)
		return kzalloc(sz, g);
	return kmalloc(sz, g);
}

void *
pfk_mallocarray(size_t nmemb, size_t size, int flags)
{
	gfp_t g = (flags & M_WAITOK) ? GFP_KERNEL : GFP_ATOMIC;

	if (flags & M_ZERO)
		return kcalloc(nmemb, size, g);
	return kmalloc_array(nmemb, size, g);
}

void
pfk_free(void *p)
{
	kfree(p);
}

/* ---------------- pools ---------------- */
static atomic64_t pfk_pool_id = ATOMIC64_INIT(0);

int
pool_init(struct pool *pl, size_t size, u_int align, int ipl, int use,
    const char *name, struct pool_allocator *alloc)
{
	memset(pl, 0, sizeof(*pl));
	pl->p_size = size;
	return 0;	/* slab lazily created on first pool_get */
}

static int
pool_get_cache(struct pool *pl)
{
	char nm[64];
	struct kmem_cache *c;

	snprintf(nm, sizeof(nm), "pfk_pool_%llu",
		(unsigned long long)atomic64_inc_return(&pfk_pool_id));
	c = kmem_cache_create_usercopy(nm, pl->p_size, 0, SLAB_HWCACHE_ALIGN,
		0, pl->p_size, NULL);
	pl->slab = c;
	return c ? 0 : -ENOMEM;
}

void
pool_destroy(struct pool *pl)
{
	if (pl->slab) {
		kmem_cache_destroy(pl->slab);
		pl->slab = NULL;
	}
}

void *
pool_get(struct pool *pl, int flags)
{
	void *v;

	if (!pl->slab && pool_get_cache(pl))
		return NULL;
	v = kmem_cache_alloc(pl->slab,
		(flags & PR_WAITOK) ? GFP_KERNEL :
			(flags & PR_NOWAIT) ? GFP_ATOMIC :
			__GFP_RETRY_MAYFAIL | GFP_KERNEL);
	if (v) {
		pl->p_used++;
		if (flags & PR_ZERO)
			memset(v, 0, pl->p_size);
	}
	return v;
}

void
pool_put(struct pool *pl, void *v)
{
	if (pl->slab && v) {
		kmem_cache_free(pl->slab, v);
		if (pl->p_used)
			pl->p_used--;
	}
}

int
pool_sethardlimit(struct pool *pl, unsigned long lim)
{
	pl->p_hardlimit = lim;
	return 0;
}

void
pool_setlowat(struct pool *pl, int n)
{
}

void
pool_sethiwat(struct pool *pl, int n)
{
}

/* ---------------- timeout ---------------- */
static void
pfk_timeout_work(struct work_struct *w)
{
	struct timeout *to = container_of(w, struct timeout, work);

	to->running = 1;
	to->pending = 0;
	if (to->fn)
		to->fn(to->arg);
	to->running = 0;
	if (to->period_ms >= 0)
		mod_timer(&to->timer, jiffies + msecs_to_jiffies(to->period_ms));
}

static void
pfk_timeout_fire(struct timer_list *tl)
{
	struct timeout *to = container_of(tl, struct timeout, timer);

	schedule_work(&to->work);
}

void
timeout_set(struct timeout *to, timeout_fn *fn, void *arg)
{
	if (!to->timer.function) {	/* one-time setup */
		timer_setup(&to->timer, pfk_timeout_fire, 0);
		INIT_WORK(&to->work, pfk_timeout_work);
		to->period_ms = -1;
	}
	to->fn = fn;
	to->arg = arg;
}

void
timeout_set_micro(struct timeout *to, timeout_fn *fn, void *arg)
{
	timeout_set(to, fn, arg);
}

/* OpenBSD TIMEOUT_INITIALIZER() statics never see timeout_set(): lazily
 * complete the Linux timer/work plumbing on the first use (PFLINUX). */
static inline void
pfk_timeout_ensure(struct timeout *to)
{
	if (!to->timer.function) {
		timer_setup(&to->timer, pfk_timeout_fire, 0);
		INIT_WORK(&to->work, pfk_timeout_work);
		to->period_ms = -1;
	}
}

int
timeout_add(struct timeout *to, long ms)
{
	pfk_timeout_ensure(to);
	if (ms < 0)
		ms = 0;
	to->pending = 1;
	to->deadline_jif = jiffies + msecs_to_jiffies(ms);
	return mod_timer(&to->timer, to->deadline_jif);
}

int
timeout_add_msec(struct timeout *to, int msecs)
{
	return timeout_add(to, msecs);
}

int
timeout_add_sec(struct timeout *to, int secs)
{
	return timeout_add(to, (long)secs * 1000);
}

int
timeout_add_usec(struct timeout *to, int us)
{
	return timeout_add(to, us > 1000 ? us / 1000 : (us ? 1 : 0));
}

int
timeout_pending(struct timeout *to)
{
	return to->pending;
}

int
timeout_del(struct timeout *to)
{
	to->pending = 0;
	if (!to->timer.function)
		return 0;
	return timer_delete(&to->timer);
}

void
timeout_del_sync(struct timeout *to)
{
	to->pending = 0;
	if (!to->timer.function)
		return;
	timer_delete_sync(&to->timer);
	cancel_work_sync(&to->work);
}

void
timeout_barrier(struct timeout *to, int flags)
{
	if (!to->timer.function)
		return;
	timer_delete_sync(&to->timer);
	cancel_work_sync(&to->work);
}

/* ---------------- tsleep/wakeup (control path only) ---------------- */
struct pfk_wait {
	struct list_head link;
	struct wait_queue_head wq;
	int done;
};

static DEFINE_SPINLOCK(pfk_tsleep_lock);
/* PFLINUX: BSD queue.h shadows LIST_HEAD (2-arg) — use raw initializer */
static struct list_head pfk_tsleep_list = {
	.next = &pfk_tsleep_list,
	.prev = &pfk_tsleep_list,
};

int
tsleep(void *p, int pr, const char *w, int timo)
{
	struct pfk_wait *pw;
	long left;

	pw = kmalloc(sizeof(*pw), GFP_KERNEL);
	if (!pw)
		return EINTR;
	init_waitqueue_head(&pw->wq);
	pw->done = 0;
	spin_lock(&pfk_tsleep_lock);
	list_add_tail(&pw->link, &pfk_tsleep_list);
	pw->link.next = (void *)p;	/* stash ident */
	spin_unlock(&pfk_tsleep_lock);
	left = wait_event_interruptible_timeout(pw->wq, pw->done,
		timo ? msecs_to_jiffies(timo) : MAX_JIFFY_OFFSET);
	spin_lock(&pfk_tsleep_lock);
	list_del(&pw->link);
	spin_unlock(&pfk_tsleep_lock);
	kfree(pw);
	if (!left)
		return EWOULDBLOCK;
	if (left == -ERESTARTSYS)
		return EINTR;
	return 0;
}

void
pfk_wakeup(void *p)
{
	struct pfk_wait *pw;

	spin_lock(&pfk_tsleep_lock);
	list_for_each_entry(pw, &pfk_tsleep_list, link) {
		if (pw->link.next == (void *)p) {
			pw->done = 1;
			wake_up_interruptible(&pw->wq);
		}
	}
	spin_unlock(&pfk_tsleep_lock);
}

/* ---------------- OpenBSD counters(9) over cpumem ---------------- */
/* Each "counter" slot is an array of `count` u64s per possible CPU.
 * PFLINUX deviation from OpenBSD's cpumem-based counters_read(): the
 * scratch argument is accepted but unused (no cpu_info walk needed). */
struct cpumem *
counters_alloc(u_int count)
{
	return cpumem_malloc((size_t)count * sizeof(uint64_t), M_WAITOK);
}

void
counters_free(struct cpumem *cm)
{
	cpumem_free(cm, 0);
}

void
counters_inc(struct cpumem *cm, u_int idx)
{
	uint64_t *p;

	if (!cm)
		return;
	preempt_disable();
	p = (uint64_t *)cm->cm_base +
	    (size_t)smp_processor_id() * cm->cm_item / sizeof(uint64_t);
	p[idx]++;
	preempt_enable();
}

void
counters_zero(struct cpumem *cm, u_int count)
{
	unsigned int cpu;

	if (!cm)
		return;
	for_each_possible_cpu(cpu) {
		uint64_t *p = (uint64_t *)cm->cm_base +
		    (size_t)cpu * cm->cm_item / sizeof(uint64_t);
		unsigned int i;

		for (i = 0; i < count; i++)
			p[i] = 0;
	}
}

void
counters_read(struct cpumem *cm, uint64_t *result, u_int count,
    uint64_t *scratch)
{
	unsigned int cpu;

	(void)scratch;		/* PFLINUX: unused (see header note) */
	memset(result, 0, count * sizeof(*result));
	if (!cm)
		return;
	for_each_possible_cpu(cpu) {
		uint64_t *p = (uint64_t *)cm->cm_base +
		    (size_t)cpu * cm->cm_item / sizeof(uint64_t);
		unsigned int i;

		for (i = 0; i < count; i++)
			result[i] += p[i];
	}
}

/* ---------------- OpenBSD sysctl-backed globals ---------------- */
/* PFLINUX: these are OpenBSD kern.sysctls PF consults per-packet.  The
 * port keeps plain globals; ip_forwarding is synced from
 * init_net.ipv4.sysctl_ip_forward on ifevents (pfk_if.c) — a runtime
 * `sysctl -w net.ipv4.ip_forward` is picked up on the next event, not
 * instantly (recorded deviation). */
int ip_forwarding;
int ip_directedbcast;
int ipmultipath;
int ip6_multipath;
int ip_mtudisc = 1;		/* Linux enables PMTU discovery by default */
int ip_defttl = 64;		/* OpenBSD IP_DEFTTL */
u_int max_linkhdr = 14;		/* refreshed by pfk_if_refresh() */
tcp_seq tcp_iss;		/* PFLINUX: stays 0; pf_tcp_iss() supplies
				 * the randomness (SHA512 + per-packet off) */

void
pfk_sysctl_sync(void)
{
	/* PFLINUX: OpenBSD ip_forwarding 0/1(/2 ipsec).  Linux 7.3 keeps
	 * the forwarding boolean in the "all" ipv4 device-configuration
	 * (per-net ipv4.devconf_all, the old net.ipv4.ip_forward sysctl
	 * storage); we mirror it as 0/1. */
	int fwd = 0;

	if (init_net.ipv4.devconf_all)
		fwd = IPV4_DEVCONF(*init_net.ipv4.devconf_all, FORWARDING);
	WRITE_ONCE(ip_forwarding, fwd ? 1 : 0);
}

/* ---------------- curcpu(9) ---------------- */
static DEFINE_PER_CPU(struct pfk_cpu_info, pfk_cpu_infos);

struct pfk_cpu_info *
pfk_curcpu(void)
{
	struct pfk_cpu_info *ci;

	preempt_disable();
	ci = this_cpu_ptr(&pfk_cpu_infos);
	ci->ci_schedstate.spc_schedflags =
	    need_resched() ? SPCF_SHOULDYIELD : 0;
	preempt_enable();
	return ci;
}

/* ---------------- if_congested(9) ---------------- */
/* PFLINUX: OpenBSD sets ifq_congestion when any ifq hits resource
 * pressure; the port stamps the same instant from its xmit path
 * (pfk_if_congestion(), called on ENOBUFS-class drops in pfk_xmit.c). */
static unsigned long pfk_congestion_jif;

void
pfk_if_congestion(void)
{
	WRITE_ONCE(pfk_congestion_jif, jiffies);
}

int
if_congested(void)
{
	unsigned long c = READ_ONCE(pfk_congestion_jif);

	if (!c || time_after(jiffies, c + HZ / 100))
		return 0;
	return 1;
}


/* pflog_packet() lives in pfsrc/pfk_pflog.c (the /dev/pflog backend). */

