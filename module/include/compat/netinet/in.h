/* PFLINUX compat: <netinet/in.h> */
#ifndef _PFK_NETINET_IN_H
#define _PFK_NETINET_IN_H
#include <linux/in.h>
#include <linux/in6.h>
/* BSD IP-level option numbers (Linux uapi uses different values) */
#define PF_IP_OPTIONS		1
#define PF_IP_RECORD_ROUTE	7
#define PF_IP_TIMESTAMP		68
#ifndef INADDR_MULTICAST
#define INADDR_MULTICAST	0xe0000000U
#endif

/* BSD <netinet/in.h> union of socket address types */
union sockaddr_union {
	struct sockaddr		su;
	struct sockaddr_in	sin;
	struct sockaddr_in6	sin6;
};
#endif
