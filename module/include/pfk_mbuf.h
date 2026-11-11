/*
 * PF-on-Linux mbuf shim: OpenBSD `struct mbuf` over Linux `sk_buff`.
 *
 * Design (see spec §4.1): PF-facing mbufs are ALWAYS single-segment
 * (linearized on ingress). `m_data`/`m_len` mirror `skb->data`/`skb->len`.
 * `m_pkthdr` carries PF's per-packet metadata (pf.* bits, ph.* aliases).
 * Chains (m_next) are used only by our own concatenation (m_cat → copy);
 * upstream PF never builds multi-segment chains it would hand back to Linux.
 *
 * (c) 2026 PF-on-Linux contributors — ISC.
 */
#ifndef _PFK_MBUF_H
#define _PFK_MBUF_H

#include <linux/skbuff.h>

struct pfk_ifnet;             /* forward (pfk_if.h) */
struct mbuf_list;             /* BSD mbuf list (pfk_if.h defines it) */
struct pf_state_key;
struct inpcb;
struct pf_tag;

struct m_tag {
	struct m_tag	*m_tag_next;
	int		 m_tag_type;
	u_int		 m_tag_len;	/* opaque: byte count of data[] */
	u_char		 m_tag_data[];
};

/* mbuf types (OpenBSD numbering) */
#define MT_FREE		0
#define MT_DATA		1
#define MT_HEADER	2
#define MT_SONAME	3
#define MT_SBUF		4
#define MT_ZOMBIE	5
#define MT_USER		6
#define MT_RTABLE	7
#define MT_IFADDR	8
#define MT_EXT		9
#define MT_RTMSG	10
#define MT_SOCKET	11
#define MT_PFDESC	12
#define MT_TAG		13
#define MT_OFDESC	14

#define MT_TAGV		10
#define MT_PFTOC	TAG_PF
#define MT_PF		TAG_PF
#define TAG_PF		0x50460000
#define TAG_PF_RR	(TAG_PF + 1)
#define MT_PFTOC	TAG_PF

struct pkthdr_pf {
	u_int			 flags;
	struct pf_state_key	*statekey;
	struct inpcb		*inp;
	u_int32_t		 qid;
	int16_t			 prio;
	u_int8_t		 delay;
	u_int8_t		 routed;
	u_int8_t		 dir;
	u_int8_t		 reason;
	u_int16_t		 ruleset;
	u_int			 tag;	/* OpenBSD: bitmask */
};

struct pkthdr {
	int			 len;
	struct pkthdr_pf	 pf;
	u_int			 csum_flags;
	void			*ph_cookie;
	u_int32_t		 ph_flowid;
	u_int			 ph_ifidx;
	u_int			 ph_rtableid;
	struct pfk_timeval	 ph_timestamp;
	struct pfk_ifnet	*ifp;
	struct pfk_ifnet	*rcvif;
	u_short			 ph;		/* tso segs: unused, kept as pad */
	u_int8_t		 af;
	u_int16_t		 proto;
	u_int			 hdr_len;
	struct m_tag		*tags;
	int			 pkthdr_len;	/* == len mirror */
};

#define M_EXT		0x0001
#define M_PKTHDR	0x0002
#define M_EOR		0x0004
#define M_RO		0x0010
#define M_BCAST		0x0020
#define M_MCAST		0x0040
#define M_LOOP		0x0080
/* m_pkthdr.csum_flags bits (OpenBSD <sys/mbuf.h> names, port values).
 * PFLINUX: the port does not trust driver offload yet; ingress packets get
 * no IN_OK bit and pf.c re-checksums in software (pf_check_tcp_cksum). */
#define M_TCP_CSUM_IN_OK	0x0001
#define M_TCP_CSUM_IN_BAD	0x0002
#define M_TCP_CSUM_OUT		0x0004
#define M_UDP_CSUM_IN_OK	0x0010
#define M_UDP_CSUM_IN_BAD	0x0020
#define M_UDP_CSUM_OUT		0x0040
#define M_ICMP_CSUM_OUT		0x0100
#define M_IPV4_CSUM_OUT		0x0200
#define M_FLOWID		0x0400
/* m_copym/m_dup "copy everything" length (OpenBSD <sys/mbuf.h>) */
#ifndef M_COPYALL
#define M_COPYALL		1000000000
#endif
/* BSD M_PREPEND (OpenBSD <sys/mbuf.h>): m = m_prepend(m, h, f) */
#ifndef M_PREPEND
#define M_PREPEND(m, h, f)	((m) = m_prepend((m), (h), (f)))
#endif
#define M_SKIP_FIREWALL	0x0100
#define M_BPF_MCAST	0x0200
#define M_PROMISC	0x0400
#define M_READONLY	0x0800
#define M_CANTFIND	(M_READONLY)
#define M_DONTFRAG	0x1000

struct mbuf {
	struct mbuf		*m_next;
	struct mbuf		*m_nextout;
	u_short			 m_flags;
	u_short			 m_len;
	caddr_t			 m_data;
	u_short			 m_type;
	u_short			 m_maxsize;
	int	 		 m_refcnt;
	struct sk_buff		*skb;	/* canonical storage (linear) */
	struct pkthdr		 m_pkthdr;	/* valid iff M_PKTHDR */
};

#define MGET(m, f, t)	do { (m) = m_get((f), (t)); } while (0)
#define MGETHDR(m, f, t) do { (m) = m_gethdr((f), (t)); } while (0)
#define mtod(m, t)	((t)((m)->m_data))
#define MTOU(m, t)	((t)((m)->m_data))
#define MFREE(m, n)	((n) = m_free((m)))

/* lifecycle */
struct mbuf *m_get(int how, short type);
struct mbuf *m_gethdr(int how, short type);
struct mbuf *m_alloc_skb(struct sk_buff *skb, int how); /* wrap borrowed skb */
struct mbuf *pfk_m_wrap_skb(struct sk_buff *skb, int af); /* ingress: extra ref */
struct mbuf *pfk_m_wrap_skb(struct sk_buff *skb, int af); /* ingress: takes extra ref */
struct mbuf *m_devget(char *buf, int totlen, int off,
		      struct pfk_ifnet *ifp, void (*copyin_fc)(caddr_t, int));
void  pfk_m_setpkthdr(struct mbuf *m);

struct mbuf *m_copym(struct mbuf *m, int off, int len, int wait);
struct mbuf *m_dup(struct mbuf *m, int wait);	/* full dup + headroom */
struct mbuf *m_prepend(struct mbuf *m, int len, int how);
struct mbuf *m_split(struct mbuf *m, int len, int flags);
struct mbuf *m_dup_pkt(struct mbuf *m, u_int hdr, int how);
#define m_copypacket(m, w) m_copym((m), 0, 0, (w))
void  m_adj(struct mbuf *m, int dlen);
void  m_trim(struct mbuf *m, int dlen);
int   m_cat(struct mbuf *to, struct mbuf *from);	/* copies+consumes from */
int m_copyback(struct mbuf *m, int off, int len, const void *copied, int type);
void  m_copydata(const struct mbuf *m, int off, int len, caddr_t dest);
struct mbuf *m_getptr(struct mbuf *m, int loc, int *off);
int   m_clump(struct mbuf **m0p, int len, int *mp_off, int *mp_len, caddr_t *doff);
void  m_free_ext(struct mbuf *m);
struct mbuf *m_free(struct mbuf *m);		/* returns next */
void  m_freem(struct mbuf *m);
struct mbuf *m_dtom(struct sk_buff *skb);	/* reverse map via skb->cb */

static inline struct sk_buff *m_to_skb(struct mbuf *m) { return m->skb; }

/* linearize: PF ingress guarantee */
int   pfk_mbuf_linearize(struct mbuf *m);	/* pskb_expand if fragged */
void  pfk_mbuf_refresh(struct mbuf *m);	/* resync m_len/pkthdr from skb */

/* m_tag */
struct m_tag *m_tag_alloc(int type, int len, int wait, caddr_t *align_data);
void  m_tag_free(struct m_tag *m);
void  m_tag_prepend(struct mbuf *m, struct m_tag *tag);
void  m_tag_prepend_packet(struct mbuf *m, struct m_tag *tag);
void  m_tag_delete(struct mbuf *m, struct m_tag *tag);
struct m_tag *m_tag_find(struct mbuf *m, int type, struct m_tag *start);
struct m_tag *m_tag_copy(struct m_tag *tag, int how);
int   m_tag_copy_chain(struct mbuf *to, struct mbuf *from, int how);
void  m_tag_delete_chain(struct mbuf *m, struct m_tag *end);
struct m_tag *m_tag_first(struct mbuf *m);
struct m_tag *m_tag_next(struct mbuf *m, struct m_tag *tag);
void  m_tag_attach(struct mbuf *m, struct m_tag *t); /* append order */

/* PF flowid/toeplitz ingress hook + sysctl-ish knobs */
int  pfk_mbuf_init(void);
void pfk_mbuf_exit(void);

#endif /* _PFK_MBUF_H */
