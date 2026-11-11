/* PFLINUX: force-include prelude for the Linux build of OpenBSD 8.0 pfctl
 * (see tools/Makefile, CPPFLAGS -include).  Keeps the copied pfctl sources
 * byte-identical to _vendor except for the edits listed in UPSTREAM.md. */
#ifndef _PFLINUX_PRELUDE_H
#define _PFLINUX_PRELUDE_H

#include <sys/types.h>
#include <sys/cdefs.h>
#include <stddef.h>	/* offsetof for sys/tree.h RBT_GENERATE */
#include <stdint.h>
#include <stdio.h>	/* fgetln(3) declaration below */
#include <stdlib.h>
#include <string.h>
#include <strings.h>	/* PFLINUX: BSD bzero/bcopy; OpenBSD <string.h>
			 * exposes them, glibc keeps them in <strings.h> */

/* PFLINUX: resolves to tools/include/sys/ioctl.h (first -I dir), which
 * redefines _IO/_IOW/_IOR/_IOWR to the OpenBSD sys/ioccom.h encoding that
 * pf.ko expects (identical to module/include/pfk.h). */
#include <sys/ioctl.h>

/* PFLINUX: BSD AF_LINK.  tools/include/net/if_dl.h maps it to AF_PACKET
 * with a sockaddr_ll-compatible struct sockaddr_dl; parse.y compares
 * n->af against AF_LINK but never includes <net/if_dl.h>, so the
 * definition lives in this prelude (if_dl.h's own copy is #ifndef'd). */
#include <sys/socket.h>
#ifndef AF_LINK
#define AF_LINK		AF_PACKET
#endif

/* OpenBSD sys/cdefs.h attribute macros that glibc does not provide. */
#ifndef __dead
#define __dead		__attribute__((__noreturn__))
#endif
#ifndef __printflike
#define __printflike(f, a)	__attribute__((__format__(__printf__, f, a)))
#endif
/* net/pfvar.h marks pfsync structs with __packed (OpenBSD cdefs); recent
 * glibc does not define it. */
#ifndef __packed
#define __packed	__attribute__((__packed__))
#endif
/* sys/tree.h's RBT_* / RB_* generator macros use __unused/__inline;
 * recent glibc sys/cdefs.h no longer provides them. */
#ifndef __unused
#define __unused	__attribute__((__unused__))
#endif
#ifndef __inline
#define __inline	inline
#endif
/*
 * OpenBSD <sys/endian.h> also spells the byte-order helpers
 * betoh16/32/64 and letoh16/32/64 (pf_print_state.c uses betoh64);
 * glibc only has
 * beXXtoh/leXXtoh names.
 */
#include <endian.h>
#ifndef betoh16
#define betoh16(x)	be16toh(x)
#endif
#ifndef betoh32
#define betoh32(x)	be32toh(x)
#endif
#ifndef betoh64
#define betoh64(x)	be64toh(x)
#endif
#ifndef letoh16
#define letoh16(x)	le16toh(x)
#endif
#ifndef letoh32
#define letoh32(x)	le32toh(x)
#endif
#ifndef letoh64
#define letoh64(x)	le64toh(x)
#endif

/*
 * OpenBSD <sys/param.h> assignment-style host<->net byte-order helpers
 * (pfctl.c uses them).  htonl/ntohl/htons/ntohs come from <netinet/in.h>
 * which the including source pulls in.
 */
#ifndef HTONL
#define HTONL(x)	((x) = htonl(x))
#endif
#ifndef NTOL
#define NTOL(x)		((x) = ntohl(x))
#endif
#ifndef HTONS
#define HTONS(x)	((x) = htons(x))
#endif
#ifndef NOSH
#define NOSH(x)		((x) = ntohs(x))
#endif

/* strtonum(3) is BSD (lib/libc/stdlib/strtonum.c); glibc has none.
 * Implemented in tools/compat-user/strtonum.c. */
long long	 strtonum(const char *numstr, long long minval,
	    long long maxval, const char **errstrp);

/* pledge(2)/unveil(2) are BSD-only.  The 8.0 pfctl sources do not call
 * them, but the stubs are provided (tools/compat-user/bsd.c) for future
 * upstream syncs. */
int		 pledge(const char *promises, const char *paths[]);
int		 unveil(const char *path, const char *permissions);

/* fgetln(3) is BSD stdio (pfctl_osfp.c uses it); implemented in
 * tools/compat-user/bsd.c on top of getline(3). */
char		*fgetln(FILE *stream, size_t *len);

/* freezero(3) is BSD stdlib (pf_ruleset.c userland branch uses it);
 * implemented in tools/compat-user/bsd.c. */
void		 freezero(void *ptr, size_t siz);

/* uid_from_user(3)/gid_from_group(3) are BSD <pwd.h>/<grp.h> helpers
 * (parse.y `user`/`group` keywords); implemented in
 * tools/compat-user/bsd.c on getpwnam(3)/getgrnam(3).  Return 0 on
 * success, non-zero with errno on failure (OpenBSD semantics). */
int		 uid_from_user(const char *, uid_t *);
int		 gid_from_group(const char *, gid_t *);

#endif /* _PFLINUX_PRELUDE_H */
