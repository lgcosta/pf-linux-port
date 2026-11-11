/* PFLINUX: <sys/timeout.h> — pfvar.h embeds struct timeout by value inside
 * struct pf_pktdelay.  pf_pktdelay is kernel-internal only (it never
 * appears in any ioctl payload; the kernel side uses the real
 * module/include/compat/sys/timeout.h with linux/timer.h), so userland
 * just needs the type to be complete for pfvar.h to compile. */
#ifndef _PFLINUX_SYS_TIMEOUT_H
#define _PFLINUX_SYS_TIMEOUT_H

#include <sys/types.h>

typedef void (timeout_fn)(void *);

struct timeout {
	timeout_fn	*fn;
	void		*arg;
};

#endif /* _PFLINUX_SYS_TIMEOUT_H */
