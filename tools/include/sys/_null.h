/* PFLINUX: <sys/_null.h> — OpenBSD's NULL guard header, required by the
 * vendored <sys/queue.h> and <sys/tree.h>. */
#ifndef _PFLINUX_SYS__NULL_H
#define _PFLINUX_SYS__NULL_H

#ifndef NULL
#ifdef __cplusplus
#define NULL	0
#else
#define NULL	((void *)0)
#endif
#endif

#endif /* _PFLINUX_SYS__NULL_H */
