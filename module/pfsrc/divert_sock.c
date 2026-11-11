/*
 * PF-on-Linux divert entry points used by pf.c (ISC 2026).
 *
 * BSD semantics (OpenBSD netinet/ip_divert.c): divert_packet()/divert6_packet()
 * find the AF_DIVERT pcb bound to `divert_port`, queue the packet to it and
 * consume the mbuf; when NO divert socket is attached, OpenBSD drops the
 * packet (m_freem) — that is exactly the observable behaviour implemented
 * here until the pfdiv socket layer is instantiated for Linux (milestone
 * M6); a rate-limited kernel log records the event so drops are never
 * silent.
 *
 * The upstream BSD socket layer is staged in pfsrc/ip_divert.c and becomes
 * the real back end in M6; these wrappers keep the same call signature
 * (struct mbuf *, int dir, u_int16_t divert_port).
 */
#include <pfk.h>
#include <net/pfvar.h>

void
divert_packet(struct mbuf *m, int dir, u_int16_t divert_port)
{
	pr_warn_ratelimited("pfk: divert v4 port %u: no divert socket layer (M6 pending), dropping\n",
	    ntohs(divert_port));
	m_freem(m);
}

void
divert6_packet(struct mbuf *m, int dir, u_int16_t divert_port)
{
	pr_warn_ratelimited("pfk: divert v6 port %u: no divert socket layer (M6 pending), dropping\n",
	    ntohs(divert_port));
	m_freem(m);
}
