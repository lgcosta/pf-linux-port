/* PFLINUX: <sys/ioctl.h> — OpenBSD 8.0 ioctl encoding for userland pfctl.
 *
 * net/pfvar.h builds every DIOC* number with _IO/_IOW/_IOR/_IOWR.  pf.ko
 * (see module/include/pfk.h) interprets ioctl commands using the OpenBSD
 * sys/ioccom.h bit layout, NOT Linux asm/ioctl.h, so this header must
 * reproduce pfk.h's PFKXIOC_* encoding byte-for-byte.  We pull in the real
 * glibc <sys/ioctl.h> first (ioctl(2) prototype, Linux SIOC* constants used
 * by <net/if.h>) and then redefine the _IO family.
 *
 * Values copied from module/include/pfk.h lines 135-152.
 */
#ifndef _PFLINUX_SYS_IOCTL_H
#define _PFLINUX_SYS_IOCTL_H

#include_next <sys/ioctl.h>

#include <sys/types.h>

#undef _IO
#undef _IOR
#undef _IOW
#undef _IOWR

#define PFKXIOC_NRBITS     8
#define PFKXIOC_TYPEBITS   8
#define PFKXIOC_SIZEBITS   14
#define PFKXIOC_DIRBITS    2
#define PFKXIOC_NRSHIFT    0
#define PFKXIOC_TYPESHIFT  (PFKXIOC_NRSHIFT + PFKXIOC_NRBITS)
#define PFKXIOC_SIZESHIFT  (PFKXIOC_TYPESHIFT + PFKXIOC_TYPEBITS)
#define PFKXIOC_DIRSHIFT   (PFKXIOC_SIZESHIFT + PFKXIOC_SIZEBITS)
#define PFKXIOC_NONE       0U
#define PFKXIOC_WRITE      1U	/* OpenBSD IOC_IN  (copyin)  */
#define PFKXIOC_READ       2U	/* OpenBSD IOC_OUT (copyout) */

#define PFKXIOC(dir,type,nr,size) \
	(((dir)  << PFKXIOC_DIRSHIFT) | ((type) << PFKXIOC_TYPESHIFT) | \
	 ((nr)   << PFKXIOC_NRSHIFT) | ((size) << PFKXIOC_SIZESHIFT))

#define _IO(t, n)           PFKXIOC(PFKXIOC_NONE, (t), (n), 0)
#define _IOWR(t, n, s)      PFKXIOC(PFKXIOC_WRITE|PFKXIOC_READ, (t), (n), sizeof(s))
#define _IOW(t, n, s)       PFKXIOC(PFKXIOC_WRITE, (t), (n), sizeof(s))
#define _IOR(t, n, s)       PFKXIOC(PFKXIOC_READ, (t), (n), sizeof(s))

#endif /* _PFLINUX_SYS_IOCTL_H */
