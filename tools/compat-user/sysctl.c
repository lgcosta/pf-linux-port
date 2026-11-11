/* PFLINUX: sysctl(3) emulation for pfctl on Linux.
 *
 * pfctl uses three OpenBSD MIBs; the legacy Linux sysctl(2) syscall this
 * replaces them with (/proc, sysconf) covers exactly those, mirroring the
 * semantics the pf.ko port provides:
 *
 *   CTL_KERN/KERN_MAXCLUSTERS  mbuf-cluster budget feeding the default
 *                              PF_LIMIT_FRAGS limit (pfctl_init_options).
 *   CTL_HW/HW_PHYSMEM64        physical memory (tiny-host table limit).
 *   CTL_NET/PF_ROUTE/NET_RT_TABLE/<id>
 *                              routing-table existence check used by
 *                              parse.y lookup_rtable().  Mirrors
 *                              module/compat/pfk_rtable.c: Linux has no
 *                              per-rtableid FIBs, so every id is accepted
 *                              (ids > RT_TABLEID_MAX never get here:
 *                              callers range-check first, and ENOENT
 *                              answers for completeness).
 */

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/socket.h>		/* PF_ROUTE (glibc value, see net/route.h note) */
#include <net/route.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*
 * Physical memory in bytes via sysconf(3); falls back to sysinfo-ish
 * estimate of 0 (callers treat it as "tiny host" path).
 */
static int64_t
pflinux_physmem(void)
{
	long pages = sysconf(_SC_PHYS_PAGES);
	long psz = sysconf(_SC_PAGESIZE);

	if (pages <= 0 || psz <= 0)
		return ((int64_t)32 * 1024 * 1024);
	return ((int64_t)pages * psz);
}

int
sysctl(const int *name, u_int namelen, void *old, size_t *oldlenp,
    const void *new, size_t newlen)
{
	(void)new;
	(void)newlen;

	if (old == NULL || oldlenp == NULL) {
		/* read-only emulation: callers always want values */
		errno = EINVAL;
		return (-1);
	}

	if (namelen == 2 && name[0] == CTL_HW && name[1] == HW_PHYSMEM64) {
		int64_t mem = pflinux_physmem();

		if (*oldlenp < sizeof(mem)) {
			errno = EINVAL;
			return (-1);
		}
		memcpy(old, &mem, sizeof(mem));
		*oldlenp = sizeof(mem);
		return (0);
	}

	if (namelen == 2 && name[0] == CTL_KERN && name[1] == KERN_MAXCLUSTERS) {
		/*
		 * PFLINUX: OpenBSD sizes nmbclusters off RAM (roughly
		 * mem/MCLBYTES, MCLBYTES==2048); Linux has no mbufs, so we
		 * hand pfctl the same RAM-derived cluster budget its
		 * defaults were designed against.
		 */
		int mcl = (int)(pflinux_physmem() / 2048);

		if (*oldlenp < sizeof(mcl)) {
			errno = EINVAL;
			return (-1);
		}
		memcpy(old, &mcl, sizeof(mcl));
		*oldlenp = sizeof(mcl);
		return (0);
	}

	if (namelen == 6 && name[0] == CTL_NET && name[1] == PF_ROUTE &&
	    name[4] == NET_RT_TABLE) {
		struct rt_tableinfo info;

		if (name[5] > RT_TABLEID_MAX) {
			errno = ENOENT;	/* nonexistent table */
			return (-1);
		}
		memset(&info, 0, sizeof(info));
		info.rti_tableid = name[5];
		info.rti_domainid = 0;
		if (*oldlenp < sizeof(info)) {
			errno = EINVAL;
			return (-1);
		}
		memcpy(old, &info, sizeof(info));
		*oldlenp = sizeof(info);
		return (0);
	}

	errno = ENOTSUP;
	return (-1);
}
