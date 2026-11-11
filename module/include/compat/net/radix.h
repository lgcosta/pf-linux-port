/* PFLINUX compat: <net/radix.h> — OpenBSD 8.0 radix structures/API
 * (rn_*), cleaned of BSD-only syntax. Implementation: compat/radix.c
 * (ported from OpenBSD 8.0 sys/net/radix.c, task T10). */
#ifndef _PFK_RADIX_H
#define _PFK_RADIX_H

#include <linux/types.h>

struct radix_mask;

struct radix_node {
	struct radix_mask	*rn_mklist;
	struct radix_node	*rn_p;
	short			rn_b;
	char			rn_bmask;
	u_char			rn_flags;
#define RNF_NORMAL	1
#define RNF_ROOT	2
#define RNF_ACTIVE	4
	union {
		struct {
			caddr_t		rn_Key;
			caddr_t		rn_Mask;
			struct radix_node *rn_Dupedkey;
		} rn_leaf;
		struct {
			int		rn_Off;
			struct radix_node *rn_L;
			struct radix_node *rn_R;
		} rn_node;
	} rn_u;
};

#define rn_dupedkey rn_u.rn_leaf.rn_Dupedkey
#define rn_key rn_u.rn_leaf.rn_Key
#define rn_mask rn_u.rn_leaf.rn_Mask
#define rn_off rn_u.rn_node.rn_Off
#define rn_l rn_u.rn_node.rn_L
#define rn_r rn_u.rn_node.rn_R

struct radix_mask {
	short			rm_b;
	char			rm_unused;
	u_char			rm_flags;
	struct radix_mask	*rm_mklist;
	union {
		caddr_t		rmu_mask;
		struct radix_node *rmu_leaf;
	} rm_rmu;
	int			rm_refs;
};

#define rm_mask rm_rmu.rmu_mask
#define rm_leaf rm_rmu.rmu_leaf

struct radix_node_head {
	struct radix_node	*rnh_treetop;
	int			rnh_addrsize;
	int			rnh_pktsize;
	struct radix_node	rnh_nodes[3];
	u_int			rnh_rtableid;
};

void	rn_init(unsigned int);
int	rn_inithead(void **, int);
int	rn_walktree(struct radix_node_head *,
	    int (*)(struct radix_node *, void *, u_int), void *);
struct radix_node *rn_addroute(void *, void *, struct radix_node_head *,
	    struct radix_node [2], u_int8_t);
struct radix_node *rn_delete(void *, void *, struct radix_node_head *,
	    struct radix_node *);
struct radix_node *rn_lookup(void *, void *, struct radix_node_head *);
struct radix_node *rn_match(void *, struct radix_node_head *);

#endif /* _PFK_RADIX_H_ */
