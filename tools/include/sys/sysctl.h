/* PFLINUX: <sys/sysctl.h> — OpenBSD sysctl(3) surface used by pfctl.
 * Linux removed both <sys/sysctl.h> and the legacy sysctl(2) syscall; the
 * constants below mirror OpenBSD 8.0 (sys/sysctl.h) and sysctl() is
 * implemented in tools/compat-user/sysctl.c for exactly the three MIBs
 * pfctl queries. */
#ifndef _PFLINUX_SYS_SYSCTL_H
#define _PFLINUX_SYS_SYSCTL_H

#include <sys/types.h>
#include <stddef.h>

#define CTL_UNSPEC		0
#define CTL_KERN		1
#define CTL_VM			2
#define CTL_NET			4	/* PF_ROUTE etc. */
#define CTL_HW			6

#define KERN_MAXCLUSTERS	67	/* int: mbuf cluster limit */
#define HW_PHYSMEM64		19	/* int64_t: total RAM */

struct kinfo_proc;		/* forward: nothing pfctl uses, avoid clash */

__BEGIN_DECLS
int	 sysctl(const int *name, u_int namelen, void *old, size_t *oldlenp,
	    const void *new, size_t newlen);
__END_DECLS

#endif /* _PFLINUX_SYS_SYSCTL_H */
