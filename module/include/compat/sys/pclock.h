/* PFLINUX compat: <sys/pclock.h> — passclock type (PF uses struct pclock
 * members for per-domain stats; kept as a simple counter here). */
#ifndef _PFK_PCLK_H
#define _PFK_PCLK_H
struct pclock {
	u_int64_t	pclock_count[4];
};
#endif
