/*
 * PF-on-Linux mbuf shim implementation (ISC, (c) 2026 PF-on-Linux contributors)
 * See pfk_mbuf.h for the invariants. All functions run under pfk NET_LOCK
 * (read side) — never in hardirq context.
 */
#include <pfk.h>

static struct kmem_cache *pfk_mbuf_cache;

struct mbuf *
m_get(int how, short type)
{
	return m_gethdr(how, type);	/* PF always uses pkthdr packets */
}

struct mbuf *
m_gethdr(int how, short type)
{
	struct mbuf *m;
	struct sk_buff *skb;

	m = kmem_cache_zalloc(pfk_mbuf_cache,
		(how & (M_WAITOK|M_CANFAIL)) == M_WAITOK ? GFP_KERNEL : GFP_ATOMIC);
	if (!m)
		return NULL;
	skb = alloc_skb(256 + NET_SKB_PAD + NET_IP_ALIGN,
		(how & (M_WAITOK|M_CANFAIL)) == M_WAITOK ? GFP_KERNEL : GFP_ATOMIC);
	if (!skb) {
		kmem_cache_free(pfk_mbuf_cache, m);
		return NULL;
	}
	skb_reserve(skb, NET_SKB_PAD + NET_IP_ALIGN);
	m->skb = skb;
	m->m_data = skb->data;
	m->m_len = skb->len;
	m->m_type = type;
	m->m_flags = M_PKTHDR;
	m->m_refcnt = 1;
	m->m_maxsize = skb_tailroom(skb);
	pfk_m_setpkthdr(m);
	return m;
}

void
pfk_m_setpkthdr(struct mbuf *m)
{
	m->m_flags |= M_PKTHDR;
	m->m_pkthdr.len = m->m_len;
	m->m_pkthdr.pf.tag = 0;
	m->m_pkthdr.tags = NULL;
}

struct mbuf *
m_alloc_skb(struct sk_buff *skb, int how)
{
	struct mbuf *m;

	m = kmem_cache_zalloc(pfk_mbuf_cache,
		(how & (M_WAITOK|M_CANFAIL)) == M_WAITOK ? GFP_KERNEL : GFP_ATOMIC);
	if (!m) {
		kfree_skb(skb);
		return NULL;
	}
	m->skb = skb;
	m->m_data = skb->data;
	m->m_len = skb->len;
	m->m_type = MT_HEADER;
	m->m_refcnt = 1;
	pfk_m_setpkthdr(m);
	return m;
}

/* reverse map: stash mbuf ptr in skb->cb (documented exclusivity in spec §4.1) */
struct pfk_skb_cb {
	struct mbuf *m;
};
#define PFK_CB(skb) ((struct pfk_skb_cb *)((skb)->cb))

struct mbuf *
m_dtom(struct sk_buff *skb)
{
	return PFK_CB(skb)->m;
}

struct mbuf *
m_devget(char *buf, int totlen, int off, struct pfk_ifnet *ifp,
	 void (*copyin_fc)(caddr_t, int))
{
	struct mbuf *m;

	if (off) {
		buf += off;
		totlen -= off;
	}
	if (totlen < 0)
		return NULL;
	m = m_gethdr(M_NOWAIT, MT_DATA);
	if (!m)
		return NULL;
	if (skb_put(m->skb, totlen) == NULL) {
		m_freem(m);
		return NULL;
	}
	if (copyin_fc)
		copyin_fc(mtod(m, caddr_t), totlen);
	else
		memcpy(m->skb->data, buf, totlen);
	pfk_mbuf_refresh(m);
	return m;
}

int
pfk_mbuf_linearize(struct mbuf *m)
{
	if (skb_is_nonlinear(m->skb)) {
		struct sk_buff *ns;

		ns = skb_copy(m->skb, GFP_ATOMIC);
		if (!ns)
			return ENOBUFS;
		consume_skb(m->skb);
		m->skb = ns;
	}
	return 0;
}

void
pfk_mbuf_refresh(struct mbuf *m)
{
	m->m_data = m->skb->data;
	m->m_len = m->skb->len;
	if (m->m_flags & M_PKTHDR)
		m->m_pkthdr.len = m->m_len;
}

struct mbuf *
m_copym(struct mbuf *m, int off, int len, int wait)
{
	struct mbuf *n;
	struct sk_buff *ns;

	if (len == 0)
		len = m->m_len - off;
	if (off > m->m_len || len > m->m_len - off)
		return NULL;
	ns = alloc_skb(len + NET_SKB_PAD + NET_IP_ALIGN,
		wait == M_WAITOK ? GFP_KERNEL : GFP_ATOMIC);
	if (!ns)
		return NULL;
	skb_reserve(ns, NET_SKB_PAD + NET_IP_ALIGN);
	if (skb_copy_bits(m->skb, off, skb_put(ns, len), len)) {
		kfree_skb(ns);
		return NULL;
	}
	n = m_alloc_skb(ns, wait);
	if (!n)
		return NULL;
	if (m->m_flags & M_PKTHDR) {
		n->m_pkthdr = m->m_pkthdr;
		n->m_pkthdr.len = len;
		n->m_pkthdr.tags = NULL;
		if (m_tag_copy_chain(n, m, wait) != 0) {
			m_freem(n);
			return NULL;
		}
	}
	return n;
}

struct mbuf *
m_dup(struct mbuf *m, int wait)
{
	struct mbuf *n = m_copym(m, 0, m->m_len, wait);

	if (n) {
		struct sk_buff *ns = n->skb;
		int need = 256 - skb_headroom(ns);

		if (need > 0 &&
		    pskb_expand_head(ns, ALIGN(need, 64), 0,
			wait == M_WAITOK ? GFP_KERNEL : GFP_ATOMIC)) {
			m_freem(n);
			return NULL;
		}
		pfk_mbuf_refresh(n);
	}
	return n;
}

void
m_adj(struct mbuf *m, int dlen)
{
	int off = dlen;

	if (off < 0) {		/* trim tail */
		skb_trim(m->skb, m->skb->len + off);
	} else if (off > 0) {
		pskb_pull(m->skb, off > m->skb->len ? m->skb->len : off);
	}
	pfk_mbuf_refresh(m);
}

void
m_trim(struct mbuf *m, int dlen)
{
	skb_trim(m->skb, dlen);
	pfk_mbuf_refresh(m);
}

int
m_cat(struct mbuf *to, struct mbuf *from)
{
	int len = from->m_len;

	if (skb_tailroom(to->skb) < len) {
		if (pskb_expand_head(to->skb, 0, ALIGN(len, 64), GFP_ATOMIC))
			return EINVAL;
		pfk_mbuf_refresh(to);
	}
	memcpy(skb_put(to->skb, len), from->m_data, len);
	pfk_mbuf_refresh(to);
	m_freem(from);
	return 0;
}

int
m_copyback(struct mbuf *m, int off, int len, const void *copied, int type)
{
	int grow = off + len - m->m_len;
	caddr_t p;

	if (off < 0 || len < 0)
		return EINVAL;
	if (grow > 0) {
		if (skb_tailroom(m->skb) < grow) {
			if (pskb_expand_head(m->skb, 0, ALIGN(grow, 64), GFP_ATOMIC))
				return ENOBUFS;
			pfk_mbuf_refresh(m);
		}
		memset(m->m_data + m->m_len, 0, grow);
		skb_put(m->skb, grow);
		pfk_mbuf_refresh(m);
	}
	p = m->m_data + off;
	memcpy(p, copied, len);
	return 0;
}

void
m_copydata(const struct mbuf *m, int off, int len, caddr_t dest)
{
	if (len <= 0)
		return;
	if (skb_copy_bits(m->skb, off, dest, len))
		memcpy(dest, m->m_data + off,
			len > m->m_len - off ? m->m_len - off : len);
}

struct mbuf *
m_getptr(struct mbuf *m, int loc, int *offp)
{
	*offp = 0;
	if (loc < 0 || loc >= m->m_len)
		return NULL;
	return m;
}

int
m_clump(struct mbuf **m0p, int len, int *mp_off, int *mp_len, caddr_t *doff)
{
	struct mbuf *m = *m0p;

	if (!pskb_may_pull(m->skb, *mp_off + len))
		return -1;
	pfk_mbuf_refresh(m);
	*mp_len = len;
	*doff = m->m_data + *mp_off;
	*m0p = m;
	return 0;
}

struct mbuf *
m_free(struct mbuf *m)
{
	struct mbuf *n = m->m_next;

	m_tag_delete_chain(m, NULL);
	if (m->skb) {
		if (PFK_CB(m->skb)->m == m)
			PFK_CB(m->skb)->m = NULL;
		kfree_skb(m->skb);
	}
	kmem_cache_free(pfk_mbuf_cache, m);
	return n;
}

void
m_freem(struct mbuf *m)
{
	while (m)
		m = m_free(m);
}

void
m_free_ext(struct mbuf *m)
{
	/* PF never attaches external mbuf refs; no-op like upstream ext free */
}

/* BSD m_removehdr: drop the packet header (length in m_pkthdr.hdr_len) */
int
m_removehdr(struct mbuf *m)
{
	int len = m->m_pkthdr.hdr_len;

	if (len <= 0)
		return 0;
	if (len > m->m_len)
		return -1;
	m_adj(m, len);
	m->m_pkthdr.hdr_len = 0;
	return 0;
}

/* BSD m_calchdrlen: header length of the (linearised) packet */
int
m_calchdrlen(struct mbuf *m)
{
	return m->m_pkthdr.hdr_len;
}

/* BSD m_tag_get: allocate a tag of `len` bytes, return pointer to data */
struct m_tag *
m_tag_get(int type, int len, int flags)
{
	struct m_tag *t;
	caddr_t data;

	t = m_tag_alloc(type, len, flags, &data);
	if (!t)
		return NULL;
	return (struct m_tag *)data;
}

/* BSD frag6_deletefraghdr: remove the IPv6 fragment header at `off` */
int
frag6_deletefraghdr(struct mbuf *m, int off)
{
	int hdrlen = sizeof(struct ip6_frag);

	if (off < 0 || off + hdrlen > m->m_len)
		return -1;
	memmove(m->m_data + off, m->m_data + off + hdrlen,
		m->m_len - off - hdrlen);
	pfk_mbuf_refresh(m);
	return 0;
}

/* ---------------- m_tag ---------------- */
struct m_tag *
m_tag_alloc(int type, int len, int wait, caddr_t *align_data)
{
	struct m_tag *t;

	t = kmalloc(sizeof(*t) + len + 4,
		wait == M_WAITOK ? GFP_KERNEL : GFP_ATOMIC);
	if (!t)
		return NULL;
	t->m_tag_next = NULL;
	t->m_tag_type = type;
	t->m_tag_len = len;
	if (align_data)
		*align_data = PFK_PTR_ALIGN(&t[1]);
	return t;
}

void
m_tag_free(struct m_tag *t)
{
	kfree(t);
}

void
m_tag_attach(struct mbuf *m, struct m_tag *t)
{
	t->m_tag_next = NULL;
	if (m->m_pkthdr.tags) {
		struct m_tag *e = m->m_pkthdr.tags;

		while (e->m_tag_next)
			e = e->m_tag_next;
		e->m_tag_next = t;
	} else
		m->m_pkthdr.tags = t;
}

void
m_tag_prepend(struct mbuf *m, struct m_tag *t)
{
	t->m_tag_next = m->m_pkthdr.tags;
	m->m_pkthdr.tags = t;
}

void
m_tag_prepend_packet(struct mbuf *m, struct m_tag *t)
{
	if (m->m_flags & M_PKTHDR)
		m_tag_prepend(m, t);
	else
		m_tag_free(t);
}

struct m_tag *
m_tag_find(struct mbuf *m, int type, struct m_tag *start)
{
	struct m_tag *t;

	if (start == NULL) {
		if (!(m->m_flags & M_PKTHDR))
			return NULL;
		t = m->m_pkthdr.tags;
	} else
		t = start->m_tag_next;
	for (; t; t = t->m_tag_next)
		if (t->m_tag_type == type)
			break;
	return t;
}

void
m_tag_delete(struct mbuf *m, struct m_tag *t)
{
	struct m_tag **pp = &m->m_pkthdr.tags, *e;

	for (e = m->m_pkthdr.tags; e; e = e->m_tag_next) {
		if (e == t) {
			*pp = t->m_tag_next;
			m_tag_free(t);
			return;
		}
		pp = &e->m_tag_next;
	}
}

struct m_tag *
m_tag_copy(struct m_tag *tag, int how)
{
	struct m_tag *t;
	caddr_t d;

	t = m_tag_alloc(tag->m_tag_type, tag->m_tag_len, how, &d);
	if (t)
		memcpy(d, tag->m_tag_data, tag->m_tag_len);
	return t;
}

int
m_tag_copy_chain(struct mbuf *to, struct mbuf *from, int how)
{
	struct m_tag *t;

	for (t = m_tag_first(from); t; t = m_tag_next(from, t)) {
		struct m_tag *n = m_tag_copy(t, how);

		if (!n)
			return ENOBUFS;
		m_tag_attach(to, n);
	}
	return 0;
}

void
m_tag_delete_chain(struct mbuf *m, struct m_tag *end)
{
	struct m_tag *t, *n;

	if (!(m->m_flags & M_PKTHDR))
		return;
	for (t = m->m_pkthdr.tags; t && t != end; t = n) {
		n = t->m_tag_next;
		m_tag_free(t);
	}
	m->m_pkthdr.tags = end;
}

struct m_tag *
m_tag_first(struct mbuf *m)
{
	return (m->m_flags & M_PKTHDR) ? m->m_pkthdr.tags : NULL;
}

struct m_tag *
m_tag_next(struct mbuf *m, struct m_tag *t)
{
	return t->m_tag_next;
}

int
pfk_mbuf_init(void)
{
	pfk_mbuf_cache = KMEM_CACHE(mbuf, 0);
	if (!pfk_mbuf_cache)
		return -ENOMEM;
	return 0;
}

void
pfk_mbuf_exit(void)
{
	kmem_cache_destroy(pfk_mbuf_cache);
}
