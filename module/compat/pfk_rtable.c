/*
 * PFLINUX rtable compatibility (ISC 2026).
 * Linux has no per-rtableid FIBs: rtable_exists() accepts every id and
 * rtable_iterate() is a terminal stub — the route-to implementation uses
 * the Linux FIB through pfk_xmit.c (pf_route/pf_route6, plan task T14).
 */
#include <pfk.h>
#include "compat/net/rtable.h"

int
rtable_exists(unsigned int rtableid)
{
	/* PFLINUX: single global FIB → every requested id exists. */
	return 1;
}

struct pf_rtentry *
rtable_iterate(struct pf_rtentry *rt)
{
	/* PFLINUX: replaced by Linux FIB walk in pfk_route_next() (T14). */
	return NULL;
}
