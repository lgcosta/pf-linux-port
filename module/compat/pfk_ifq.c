/*
 * PF-on-Linux ifq (transmit queue) framework (ISC 2026)
 *
 * Implements the OpenBSD 8.0 sys/net/ifq.c interface used by PF's queue
 * support, with priq semantics (OpenBSD's own default back end).  The
 * OpenBSD hfsc.c / fq_codel.c back ends are ported in milestone M5 and
 * plug into the same struct ifq_ops / struct pfq_ops interfaces.
 */
#include <pfk.h>
#include "compat/net/ifq.h"

/* One queue slot per priority level (OpenBSD's priq uses 32 slots). */
#define PFQ_PRIQ_SLOTS	32

struct pfk_priq {
	struct mbuf	*head[PFQ_PRIQ_SLOTS];
	struct mbuf	*tail[PFQ_PRIQ_SLOTS];
	unsigned int	 len[PFQ_PRIQ_SLOTS];
};

static unsigned int
pfk_priq_idx(unsigned int nifqs, const struct mbuf *m)
{
	int prio = 0;

	if (m && m->m_pkthdr.pf.prio)
		prio = m->m_pkthdr.pf.prio;
	if (prio < 0)
		prio = 0;
	if (prio >= PFQ_PRIQ_SLOTS)
		prio = PFQ_PRIQ_SLOTS - 1;
	(void)nifqs;
	return (unsigned int)prio;
}

static struct mbuf *
pfk_priq_enq(struct ifqueue *ifq, struct mbuf *m)
{
	struct pfk_priq *pq = ifq->ifq_softc;
	unsigned int idx = pfk_priq_idx(0, m);

	if (!pq)
		return NULL;
	if (!pq->head[idx]) {
		pq->head[idx] = m;
		m->m_next = NULL;
	} else {
		pq->tail[idx]->m_next = m;
		m->m_next = NULL;
	}
	pq->tail[idx] = m;
	pq->len[idx]++;
	ifq->ifq_len++;
	ifq->ifq_bytes += m->m_len;
	return m;
}

static struct mbuf *
pfk_priq_deq_begin(struct ifqueue *ifq, void **cookie)
{
	struct pfk_priq *pq = ifq->ifq_softc;
	unsigned int idx;

	*cookie = NULL;
	if (!pq)
		return NULL;
	for (idx = 0; idx < PFQ_PRIQ_SLOTS; idx++) {
		if (pq->head[idx]) {
			struct mbuf *m = pq->head[idx];

			pq->head[idx] = m->m_next;
			if (!pq->head[idx])
				pq->tail[idx] = NULL;
			pq->len[idx]--;
			m->m_next = NULL;
			*cookie = (void *)(uintptr_t)idx;
			return m;
		}
	}
	return NULL;
}

static void
pfk_priq_deq_commit(struct ifqueue *ifq, struct mbuf *m, void *cookie)
{
	(void)cookie;
	if (m) {
		ifq->ifq_len--;
		ifq->ifq_bytes -= m->m_len;
	}
}

static void
pfk_priq_purge(struct ifqueue *ifq, struct mbuf_list *ml)
{
	struct pfk_priq *pq = ifq->ifq_softc;
	unsigned int idx;

	if (!pq)
		return;
	for (idx = 0; idx < PFQ_PRIQ_SLOTS; idx++) {
		struct mbuf *m = pq->head[idx];

		pq->head[idx] = pq->tail[idx] = NULL;
		pq->len[idx] = 0;
		while (m) {
			struct mbuf *n = m->m_next;

			m->m_next = NULL;
			if (ml) {
				if (ml->ml_last)
					ml->ml_last->m_next = m;
				else
					ml->ml_first = m;
				ml->ml_last = m;
				ml->ml_len++;
			} else
				m_freem(m);
			m = n;
		}
	}
	ifq->ifq_len = 0;
	ifq->ifq_bytes = 0;
}

static void *
pfk_priq_alloc(unsigned int size, void *arg)
{
	struct pfk_priq *pq = kzalloc(sizeof(*pq), GFP_KERNEL);

	(void)size;
	(void)arg;
	return pq;
}

static void
pfk_priq_free(unsigned int size, void *arg)
{
	(void)size;
	kfree(arg);
}

static const struct ifq_ops pfk_priq_ifq_ops = {
	.ifqop_idx	= pfk_priq_idx,
	.ifqop_enq	= pfk_priq_enq,
	.ifqop_deq_begin	= pfk_priq_deq_begin,
	.ifqop_deq_commit = pfk_priq_deq_commit,
	.ifqop_purge	= pfk_priq_purge,
	.ifqop_alloc	= pfk_priq_alloc,
	.ifqop_free	= pfk_priq_free,
};

const struct ifq_ops * const ifq_priq_ops = &pfk_priq_ifq_ops;

/*
 * PF queue manager back end (pfq_ops): wraps the ifq back end so that PF
 * can attach a queue to an interface and query its statistics.
 */
struct pfk_pfq {
	struct ifqueue	 ifq;
	struct pfk_ifnet *ifp;
};

static void *
pfk_pfq_alloc(struct pfk_ifnet *ifp)
{
	struct pfk_pfq *pfq = kzalloc(sizeof(*pfq), GFP_KERNEL);

	if (!pfq)
		return NULL;
	pfq->ifp = ifp;
	ifq_init(&pfq->ifq, ifp, IFQ_MAXLEN);
	return pfq;
}

static int
pfk_pfq_addqueue(void *arg, struct pf_queuespec *qspec)
{
	(void)qspec;
	return arg ? 0 : EINVAL;
}

static void
pfk_pfq_free(void *arg)
{
	struct pfk_pfq *pfq = arg;

	if (!pfq)
		return;
	ifq_destroy(&pfq->ifq);
	kfree(pfq);
}

static int
pfk_pfq_qstats(struct pf_queuespec *qspec, void *arg, int *pkts)
{
	struct pfk_pfq *pfq = arg;

	(void)qspec;
	if (!pfq || !pkts)
		return EINVAL;
	*pkts = (int)pfq->ifq.ifq_len;
	return 0;
}

static unsigned int
pfk_pfq_qlength(void *arg)
{
	struct pfk_pfq *pfq = arg;

	return pfq ? pfq->ifq.ifq_len : 0;
}

static struct mbuf *
pfk_pfq_enqueue(void *arg, struct mbuf *m)
{
	struct pfk_pfq *pfq = arg;

	if (!pfq)
		return NULL;
	if (ifq_enqueue(&pfq->ifq, m) != 0)
		return NULL;
	return m;
}

static struct mbuf *
pfk_pfq_deq_begin(void *arg, void **cookie, struct mbuf_list *ml)
{
	struct pfk_pfq *pfq = arg;

	(void)ml;
	if (!pfq)
		return NULL;
	return ifq_deq_begin(&pfq->ifq);
}

static void
pfk_pfq_deq_commit(void *arg, struct mbuf *m, void *cookie)
{
	struct pfk_pfq *pfq = arg;

	(void)cookie;
	if (pfq)
		ifq_deq_commit(&pfq->ifq, m);
}

static void
pfk_pfq_purge(void *arg, struct mbuf_list *ml)
{
	struct pfk_pfq *pfq = arg;

	if (pfq)
		ifq_purge(&pfq->ifq);
	(void)ml;
}

static const struct pfq_ops pfk_priq_pfq_ops = {
	.pfq_alloc	= pfk_pfq_alloc,
	.pfq_addqueue	= pfk_pfq_addqueue,
	.pfq_free	= pfk_pfq_free,
	.pfq_qstats	= pfk_pfq_qstats,
	.pfq_qlength	= pfk_pfq_qlength,
	.pfq_enqueue	= pfk_pfq_enqueue,
	.pfq_deq_begin	= pfk_pfq_deq_begin,
	.pfq_deq_commit	= pfk_pfq_deq_commit,
	.pfq_purge	= pfk_pfq_purge,
};

/*
 * HFSC / fq_codel back ends: OpenBSD 8.0 ships sys/net/hfsc.c and
 * sys/net/fq_codel.c; those files are ported in milestone M5.  Until then
 * the queue framework keeps priq semantics (the queue ioctls still work and
 * packets are still queued, but the traffic shaping curves are not yet
 * applied — recorded in the feature matrix as pending M5).
 */
/*
 * M5: the real scheduler back ends (pfsrc/hfsc.c, pfsrc/fq_codel.c) define
 * ifq_hfsc_ops/pfq_hfsc_ops/ifq_fqcodel_ops/pfq_fqcodel_ops themselves; the
 * priq shim above stays as the default for interfaces without a queue rule.
 */

/* ---------------- framework ---------------- */
void
ifq_init(struct ifqueue *ifq, struct pfk_ifnet *ifp, unsigned int maxlen)
{
	memset(ifq, 0, sizeof(*ifq));
	ifq->ifq_if = ifp;
	ifq->ifq_maxlen = maxlen ? maxlen : IFQ_MAXLEN;
	ifq_attach(ifq, ifq_priq_ops, NULL);
}

void
ifq_init_maxlen(struct ifqueue *ifq, unsigned int maxlen)
{
	ifq->ifq_maxlen = maxlen;
}

void
ifq_attach(struct ifqueue *ifq, const struct ifq_ops *ops, void *opsarg)
{
	if (!ops)
		return;
	if (!opsarg && ops->ifqop_alloc)
		opsarg = ops->ifqop_alloc(0, NULL);
	ifq->ifq_ops = ops;
	ifq->ifq_softc = opsarg;
}

/*
 * Scheduler hand-off used by the ported back ends (hfsc/fq_codel) when they
 * need their per-interface state.  PFLINUX: the port has no ifq mutex (the
 * data path is a single ordered work queue), so "enter" is a plain ownership
 * check and "leave" is a no-op - the pair exists to mirror OpenBSD's API.
 */
void *
ifq_q_enter(struct ifqueue *ifq, const struct ifq_ops *ops)
{
	if (ifq == NULL || ops == NULL || ifq->ifq_ops != ops)
		return NULL;			/* another scheduler owns it */
	if (ifq->ifq_softc == NULL && ops->ifqop_alloc)
		ifq->ifq_softc = ops->ifqop_alloc(0, NULL);
	return ifq->ifq_softc;
}

void
ifq_q_leave(struct ifqueue *ifq, void *q)
{
	(void)ifq;
	(void)q;
}

void
ifq_destroy(struct ifqueue *ifq)
{
	if (ifq->ifq_ops && ifq->ifq_ops->ifqop_free)
		ifq->ifq_ops->ifqop_free(0, ifq->ifq_softc);
	ifq->ifq_ops = NULL;
	ifq->ifq_softc = NULL;
}

int
ifq_enqueue(struct ifqueue *ifq, struct mbuf *m)
{
	if (!ifq->ifq_ops || !ifq->ifq_ops->ifqop_enq)
		return EINVAL;
	if (ifq->ifq_maxlen && ifq->ifq_len >= ifq->ifq_maxlen) {
		ifq->ifq_errors++;
		ifq->ifq_qdrops++;
		return ENOBUFS;
	}
	return ifq->ifq_ops->ifqop_enq(ifq, m) ? 0 : ENOMEM;
}

struct mbuf *
ifq_deq_begin(struct ifqueue *ifq)
{
	struct mbuf *m;
	void *cookie = NULL;

	if (!ifq->ifq_ops || !ifq->ifq_ops->ifqop_deq_begin)
		return NULL;
	m = ifq->ifq_ops->ifqop_deq_begin(ifq, &cookie);
	return m;
}

void
ifq_deq_commit(struct ifqueue *ifq, struct mbuf *m)
{
	void *cookie = NULL;

	if (ifq->ifq_ops && ifq->ifq_ops->ifqop_deq_commit)
		ifq->ifq_ops->ifqop_deq_commit(ifq, m, cookie);
}

void
ifq_deq_rollback(struct ifqueue *ifq, struct mbuf *m)
{
	if (m)
		ifq_enqueue(ifq, m);
}

struct mbuf *
ifq_dequeue(struct ifqueue *ifq)
{
	struct mbuf *m = ifq_deq_begin(ifq);

	if (m)
		ifq_deq_commit(ifq, m);
	return m;
}

void
ifq_start(struct ifqueue *ifq)
{
	ifq->ifq_oactive = 0;
}

unsigned int
ifq_purge(struct ifqueue *ifq)
{
	unsigned int n = ifq->ifq_len;

	if (ifq->ifq_ops && ifq->ifq_ops->ifqop_purge)
		ifq->ifq_ops->ifqop_purge(ifq, NULL);
	return n;
}

void
ifq_mfreem(struct ifqueue *ifq, struct mbuf *m)
{
	(void)ifq;
	m_freem(m);
}

void
ifq_mfreeml(struct ifqueue *ifq, struct mbuf_list *ml)
{
	struct mbuf *m;

	(void)ifq;
	while (ml && (m = ml->ml_first) != NULL) {
		ml->ml_first = m->m_next;
		if (!ml->ml_first)
			ml->ml_last = NULL;
		ml->ml_len--;
		m->m_next = NULL;
		m_freem(m);
	}
}

void
ifq_set_oactive(struct ifqueue *ifq)
{
	ifq->ifq_oactive = 1;
}

void
ifq_deq_set_oactive(struct ifqueue *ifq)
{
	ifq->ifq_oactive = 1;
}

int
ifq_hdatalen(struct ifqueue *ifq)
{
	(void)ifq;
	return 0;
}

