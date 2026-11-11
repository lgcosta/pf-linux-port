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

/* ---------------- NET_LOCK ---------------- */
static DECLARE_RWSEM(pfk_net_sem);
static atomic_t pfk_net_readers = ATOMIC_INIT(0);

void pfk_net_lock_init(void) { }
void pfk_net_lock(void)        { down_write(&pfk_net_sem); }
void pfk_net_unlock(void)      { up_write(&pfk_net_sem); }
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

void
rw_enter(struct rwlock *l, int mode)
{
	if (mode == RW_WRITER) {
		down_write(&l->sem);
		l->is_writer = 1;
	} else {
		down_read(&l->sem);
		atomic_inc(&l->readers);
	}
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

void
pfk_free(void *p)
{
	kfree(p);
}

/* ---------------- pools ---------------- */
static atomic64_t pfk_pool_id = ATOMIC64_INIT(0);

int
pool_init(struct pool *pl, size_t size, int align, int ipl, const char *use,
	  void *p, int flags, void (*ih)(void *, void *), void *arg,
	  struct srcptr_static *sp)
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

void
pool_sethardlimit(struct pool *pl, unsigned long lim, const char *msg, int lvl)
{
	pl->p_hardlimit = lim;
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
	if (!to->fn) {		/* one-time setup */
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

int
timeout_add(struct timeout *to, long ms)
{
	if (ms < 0)
		ms = 0;
	to->pending = 1;
	to->deadline_jif = jiffies + msecs_to_jiffies(ms);
	return mod_timer(&to->timer, to->deadline_jif);
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
	return timer_delete(&to->timer);
}

void
timeout_del_sync(struct timeout *to)
{
	to->pending = 0;
	timer_delete_sync(&to->timer);
	cancel_work_sync(&to->work);
}

void
timeout_barrier(struct timeout *to, int flags)
{
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
