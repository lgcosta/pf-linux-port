/* PFLINUX compat: <sys/rwlock.h> — struct pfk_rwlock alias provided by
 * pfk.h before this point (include-guard re-entrancy); wrapper is a marker. */
#ifndef _PFK_RWLOCK_H
#define _PFK_RWLOCK_H
/* struct rwlock == struct pfk_rwlock (see pfk.h) */
#define LCK_MRW		0
#endif
