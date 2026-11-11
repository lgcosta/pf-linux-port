/* PFLINUX compat: <sys/refcnt.h> — refcount_t wrapper with OpenBSD API */
#ifndef _PFK_REFCNT_H
#define _PFK_REFCNT_H
#include <linux/refcount.h>

struct refcnt { refcount_t rc; };
#define REFCNT_INITIALIZER	{ .rc = REFCNT_INIT }

static inline void refcnt_init(struct refcnt *r)
{
	refcount_set(&r->rc, 1);
}
static inline unsigned int refcnt_value(const struct refcnt *r)
{
	return refcount_read((refcount_t *)&r->rc);
}
static inline void refcnt_take(struct refcnt *r)
{
	refcount_inc(&r->rc);
}
/* returns true if ref was valid and taken (OpenBSD refcnt_take_if) */
static inline bool refcnt_take_not0(struct refcnt *r)
{
	return refcount_inc_not_zero(&r->rc);
}
/*
 * OpenBSD semantics (sys/kern/kern_synch.c): refcnt_rele() returns 1 when
 * the last reference is dropped and 0 otherwise.  PF_REF_RELE() and the
 * pf_ruleset anchor release rely on that polarity.
 */
static inline unsigned int refcnt_rele(struct refcnt *r)
{
	return (unsigned int)refcount_dec_and_test(&r->rc) ? 1 : 0;
}
static inline unsigned int refcnt_rele_wake(struct refcnt *r)
{
	return refcnt_rele(r);
}
static inline void refcnt_finalize(struct refcnt *r, const void *unused)
{
}
#endif
