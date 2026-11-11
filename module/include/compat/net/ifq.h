/* PFLINUX compat: <net/ifq.h> — OpenBSD 8.0 transmit-queue framework.
 * Field names follow upstream (ifqop_*) so the ported PF code stays
 * line-proximate.  The framework itself is implemented in compat/pfk_ifq.c
 * (priq semantics); the HFSC and fq_codel back ends are ported in
 * milestone M5 and plug into the same ops structures.
 */
#ifndef _PFK_IFQ_H
#define _PFK_IFQ_H

#include <linux/types.h>

/* struct pfq_ops comes from net/pfvar.h (OpenBSD 8.0) — it is included by
 * pfk.h before this header. */
struct mbuf;
struct mbuf_list;
struct pfk_ifnet;
struct pfk_ifnet;
struct pf_queuespec;

#define IFQ_MAXLEN	256

struct ifqueue {
	struct pfk_ifnet	*ifq_if;
	void			*ifq_softc;
	const struct ifq_ops	*ifq_ops;
	void			*ifq_q;
	struct mbuf_list	*ifq_free;	/* free list (unused in the port) */
	unsigned int		 ifq_len;
	unsigned int		 ifq_oactive;

	/* statistics */
	u_int64_t		 ifq_packets;
	u_int64_t		 ifq_bytes;
	u_int64_t		 ifq_qdrops;
	u_int64_t		 ifq_errors;
	u_int64_t		 ifq_mcasts;
	u_int32_t		 ifq_oactives;

	void			*ifq_kstat;

	/* properties */
	unsigned int		 ifq_maxlen;
	unsigned int		 ifq_idx;
};

struct ifq_ops {
	unsigned int	(*ifqop_idx)(unsigned int, const struct mbuf *);
	struct mbuf	*(*ifqop_enq)(struct ifqueue *, struct mbuf *);
	struct mbuf	*(*ifqop_deq_begin)(struct ifqueue *, void **);
	void		(*ifqop_deq_commit)(struct ifqueue *, struct mbuf *,
			    void *);
	void		(*ifqop_purge)(struct ifqueue *, struct mbuf_list *);
	void		*(*ifqop_alloc)(unsigned int, void *);
	void		(*ifqop_free)(unsigned int, void *);
};

extern const struct ifq_ops * const ifq_priq_ops;
extern const struct ifq_ops * const ifq_hfsc_ops;
extern const struct ifq_ops * const ifq_fqcodel_ops;
extern const struct pfq_ops * const pfq_hfsc_ops;
extern const struct pfq_ops * const pfq_fqcodel_ops;

void		 ifq_init(struct ifqueue *, struct pfk_ifnet *, unsigned int);
void		 ifq_init_maxlen(struct ifqueue *, unsigned int);
void		 ifq_attach(struct ifqueue *, const struct ifq_ops *, void *);
void		 ifq_destroy(struct ifqueue *);
int		 ifq_enqueue(struct ifqueue *, struct mbuf *);
void		 ifq_start(struct ifqueue *);
struct mbuf	*ifq_deq_begin(struct ifqueue *);
void		 ifq_deq_commit(struct ifqueue *, struct mbuf *);
void		 ifq_deq_rollback(struct ifqueue *, struct mbuf *);
struct mbuf	*ifq_dequeue(struct ifqueue *);
unsigned int	 ifq_purge(struct ifqueue *);
void		 ifq_mfreem(struct ifqueue *, struct mbuf *);
void		 ifq_mfreeml(struct ifqueue *, struct mbuf_list *);
void		 ifq_set_oactive(struct ifqueue *);
void		 ifq_deq_set_oactive(struct ifqueue *);
int		 ifq_hdatalen(struct ifqueue *);

#define ifq_len(_ifq)		READ_ONCE((_ifq)->ifq_len)
#define ifq_empty(_ifq)		(ifq_len(_ifq) == 0)

static inline int
ifq_is_priq(struct ifqueue *ifq)
{
	return (ifq->ifq_ops == ifq_priq_ops);
}

static inline void
ifq_clr_oactive(struct ifqueue *ifq)
{
	ifq->ifq_oactive = 0;
}

static inline unsigned int
ifq_is_oactive(struct ifqueue *ifq)
{
	return (ifq->ifq_oactive);
}

static inline unsigned int
ifq_idx(struct ifqueue *ifq, unsigned int nifqs, const struct mbuf *m)
{
	return ((*ifq->ifq_ops->ifqop_idx)(nifqs, m));
}

#endif
