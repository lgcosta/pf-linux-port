/* PFLINUX compat: <sys/socket.h> — BSD sockaddrs stay Linux-layout;
 * sa_len fields are removed by the staging sed (see PORTING.md).
 * Provides BSD sockaddr accessor macros + address family constants. */
#ifndef _PFK_SOCKET_H
#define _PFK_SOCKET_H

#include <linux/types.h>
struct pfk_sockaddr_union {
	struct sockaddr_storage ss;
};

#define sintosa(x)	((struct sockaddr_in *)(void *)(x))
#define sin6tosa(x)	((struct sockaddr_in6 *)(void *)(x))
#define sa_tosa(x)	((struct sockaddr *)(void *)(x))
#define satosa(x)	((struct sockaddr *)(void *)(x))
#define satoxp(x)	((void *)(x))	/* unused; keep symbol */
#define SA_LEN(x)       (((x)->sa_family == AF_INET) ? \
			    sizeof(struct sockaddr_in) : \
			((x)->sa_family == AF_INET6) ? \
			    sizeof(struct sockaddr_in6) : 16)
#define PFK_SA(x)	((struct sockaddr *)(x))
#define PFK_INA(x)	(&((struct sockaddr_in *)(x))->sin_addr)
#define PFK_SA6(x)	((struct sockaddr_in6 *)(x))

/* BSD sin_port/sin_addr == Linux: both network byte order fields */
#ifndef SIN_IS_PRIVATE
#define SIN_IS_PRIVATE  1
#endif
/* AF_INET6 == 10 on both sides */
#ifndef AF_LINK
#define AF_LINK         18	/* OpenBSD value; used by pf for link sa */
#endif

static inline int pfk_sa_cmp(const struct sockaddr *a, const struct sockaddr *b)
{
	if (a->sa_family != b->sa_family)
		return 1;
	switch (a->sa_family) {
	case AF_INET:
		return memcmp(sintosa(a), sintosa(b), sizeof(struct sockaddr_in));
	case AF_INET6:
		return memcmp(sin6tosa(a), sin6tosa(b), sizeof(struct sockaddr_in6));
	}
	return 1;
}
#endif
