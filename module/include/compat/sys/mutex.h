/* PFLINUX compat: <sys/mutex.h> — OpenBSD struct mutex over a Linux
 * counting semaphore. BSD entry points are MACROS calling pfk_* helpers,
 * after #undef of any same-named kernel macros (the kernel's own mutex_*
 * functions were expanded before pfk.h and are unaffected). */
#ifndef _PFK_SYS_MUTEX_H
#define _PFK_SYS_MUTEX_H

#define IPL_SOFTNET	6

struct pfk_mutex {
	struct semaphore	s;
	const char		*name;
};

void pfk_mutex_setup(struct pfk_mutex *m, const char *name);

#define PFK_MUTEX_INIT(obj) \
	{ .s = __SEMAPHORE_INITIALIZER(obj, 1), .name = #obj }

#undef mutex_init
#undef mutex_destroy
#undef mutex_is_locked
#define mutex_init(m, n, f)		pfk_mutex_setup((m), (n))
#define mutex_enter(m)			down(&(m)->s)
#define mutex_leave(m)			up(&(m)->s)
#define mutex_try_enter(m, t)		(down_trylock(&(m)->s) == 0)
#define mutex_destroy(m)		do { } while (0)
#define mutex_is_locked(m)		(atomic_long_read(&(m)->s.count) < 1)

/* MUTEX_INITIALIZER(ipl) used in static definitions:
 *   struct mutex x = MUTEX_INITIALIZER(IPL_SOFTNET); */
#define MUTEX_INITIALIZER(ipl)		PFK_MUTEX_INIT(pfk_mtx_static)

#endif
