/* PFLINUX: <sys/refcnt.h> — userland twin of
 * module/include/compat/sys/refcnt.h.  The kernel side is
 * `struct refcnt { refcount_t rc; }` where refcount_t wraps a single
 * atomic_t (4 bytes on Linux).  Only struct pf_anchor embeds it and
 * pf_anchor never crosses /dev/pf, but size/alignment are kept equal. */
#ifndef _PFLINUX_SYS_REFCNT_H
#define _PFLINUX_SYS_REFCNT_H

#include <sys/types.h>

struct refcnt {
	unsigned int	rc;
};

#define REFCNT_INITIALIZER	{ .rc = 1 }

static inline void
refcnt_init(struct refcnt *r, unsigned int v)
{
	r->rc = v ? v : 1;
}

static inline unsigned int
refcnt_value(const struct refcnt *r)
{
	return (r->rc);
}

static inline void
refcnt_take(struct refcnt *r)
{
	r->rc++;
}

static inline int
refcnt_take_not0(struct refcnt *r)
{
	if (r->rc == 0)
		return (0);
	r->rc++;
	return (1);
}

static inline unsigned int
refcnt_rele(struct refcnt *r)
{
	return (--r->rc);
}

static inline unsigned int
refcnt_rele_wake(struct refcnt *r)
{
	return (refcnt_rele(r));
}

static inline void
refcnt_finalize(struct refcnt *r, const void *unused)
{
	(void)r;
	(void)unused;
}

#endif /* _PFLINUX_SYS_REFCNT_H */
