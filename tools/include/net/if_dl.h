/* PFLINUX: <net/if_dl.h> — BSD link-layer address.  Linux has no AF_LINK;
 * getifaddrs() reports link-layer addresses as AF_PACKET/struct sockaddr_ll.
 * We therefore define AF_LINK as AF_PACKET and lay struct sockaddr_dl out
 * exactly like struct sockaddr_ll, so pfctl_parser.c's
 * ((struct sockaddr_dl *)ifa->ifa_addr)->sdl_index finds sll_ifindex.
 * Everything stays userland-internal: af values tagged AF_LINK never cross
 * /dev/pf (rules carry ifname strings, not sockaddr families). */
#ifndef _PFLINUX_NET_IF_DL_H
#define _PFLINUX_NET_IF_DL_H

#include <sys/types.h>
#include <sys/socket.h>

#ifndef AF_LINK
#define AF_LINK		AF_PACKET	/* PFLINUX mapping, see header note */
#endif

struct sockaddr_dl {
	sa_family_t	sdl_family;	/* sll_family   (offset 0)  */
	u_short		sdl_protocol;	/* sll_protocol (offset 2)  */
	int		sdl_index;	/* sll_ifindex  (offset 4)  */
	u_short		sdl_type;	/* sll_hatype   (offset 8)  */
	u_char		sdl_pkttype;	/* sll_pkttype  (offset 10) */
	u_char		sdl_alen;	/* sll_halen    (offset 11) */
	u_char		sdl_data[8];	/* sll_addr     (offset 12) */
};

#define LLADDR(s)	((caddr_t *)((s)->sdl_data))

#endif /* _PFLINUX_NET_IF_DL_H */
