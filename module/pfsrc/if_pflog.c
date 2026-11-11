/* PFLINUX port of OpenBSD 8.0 net/if_pflog.c — see docs/porting-notes.md */
#include <pfk.h>

#define PFLOGMTU	(32768 + MHLEN + MLEN)

#ifdef PFLOGDEBUG
#define DPRINTF(x)    do { if (pflogdebug) printf x ; } while (0)
#else
#define DPRINTF(x)
#endif

void	pflogattach(int);
int	pflogoutput(struct ifnet *, struct mbuf *, struct sockaddr *,
		       struct rtentry *);
int	pflogioctl(struct ifnet *, u_long, caddr_t);
int	pflog_clone_create(struct if_clone *, int);
int	pflog_clone_destroy(struct ifnet *);
struct	pflog_softc	*pflog_getif(int);

struct if_clone			pflog_cloner =
    IF_CLONE_INITIALIZER("pflog", pflog_clone_create, pflog_clone_destroy);

LIST_HEAD(, pflog_softc)	pflog_ifs = LIST_HEAD_INITIALIZER(pflog_ifs);

void
pflogattach(int npflog)
{
	if_clone_attach(&pflog_cloner);
}

int
pflog_clone_create(struct if_clone *ifc, int unit)
{
	struct ifnet *ifp;
	struct pflog_softc *pflogif;

	pflogif = malloc(sizeof(*pflogif), M_DEVBUF, M_WAITOK|M_ZERO);
	pflogif->sc_unit = unit;
	ifp = &pflogif->sc_if;
	snprintf(ifp->if_xname, sizeof ifp->if_xname, "pflog%d", unit);
	ifp->if_softc = pflogif;
	ifp->if_mtu = PFLOGMTU;
	ifp->if_ioctl = pflogioctl;
	ifp->if_output = pflogoutput;
	ifp->if_xflags = IFXF_CLONED;
	ifp->if_type = IFT_PFLOG;
	ifp->if_hdrlen = PFLOG_HDRLEN;
	if_attach(ifp);
	if_alloc_sadl(ifp);

#if NBPFILTER > 0
	bpfattach(&pflogif->sc_if.if_bpf, ifp, DLT_PFLOG, PFLOG_HDRLEN);
#endif

	NET_LOCK();
	LIST_INSERT_HEAD(&pflog_ifs, pflogif, sc_entry);
	NET_UNLOCK();

	return (0);
}

int
pflog_clone_destroy(struct ifnet *ifp)
{
	struct pflog_softc	*pflogif = ifp->if_softc;

	NET_LOCK();
	LIST_REMOVE(pflogif, sc_entry);
	NET_UNLOCK();

	if_detach(ifp);
	free(pflogif, M_DEVBUF, sizeof(*pflogif));

	return (0);
}

int
pflogoutput(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
	struct rtentry *rt)
{
	m_freem(m);	/* drop packet */
	return (EAFNOSUPPORT);
}

int
pflogioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	switch (cmd) {
	case SIOCSIFFLAGS:
		if (ifp->if_flags & PFIFF_UP)
			ifp->if_flags |= PFIFF_RUNNING;
		else
			ifp->if_flags &= ~PFIFF_RUNNING;
		break;
	default:
		return (ENOTTY);
	}

	return (0);
}

struct pflog_softc *
pflog_getif(int unit)
{
	struct pflog_softc *pflogif;

	NET_ASSERT_LOCKED();

	LIST_FOREACH(pflogif, &pflog_ifs, sc_entry) {
		if (pflogif->sc_unit == unit)
			break;
	}

	return pflogif;
}

int
pflog_packet(struct pf_pdesc *pd, u_int8_t reason, struct pf_rule *rm,
    struct pf_rule *am, struct pf_ruleset *ruleset, struct pf_rule *trigger)
{
#if NBPFILTER > 0
	struct pflog_softc *pflogif;
	struct ifnet *ifn;
	caddr_t if_bpf;
	struct pfloghdr hdr;

	if (rm == NULL || pd == NULL || pd->kif == NULL || pd->m == NULL)
		return (-1);
	if (trigger == NULL)
		trigger = rm;
	pflogif = pflog_getif(trigger->logif);
	if (pflogif == NULL)
		return (0);
	ifn = &pflogif->sc_if;
	if_bpf = ifn->if_bpf;
	if (!if_bpf)
		return (0);

	bzero(&hdr, sizeof(hdr));
	hdr.length = PFLOG_REAL_HDRLEN;
	/* Default rule does not pass packets dropped for other reasons. */
	hdr.action = (rm->nr == (u_int32_t)-1 && reason != PFRES_MATCH) ?
	    PF_DROP : rm->action;
	hdr.reason = reason;
	memcpy(hdr.ifname, pd->kif->pfik_name, sizeof(hdr.ifname));

	if (am == NULL) {
		hdr.rulenr = htonl(rm->nr);
		hdr.subrulenr = -1;
	} else {
		hdr.rulenr = htonl(am->nr);
		hdr.subrulenr = htonl(rm->nr);
		if (ruleset != NULL && ruleset->anchor != NULL)
			strlcpy(hdr.ruleset, ruleset->anchor->name,
			    sizeof(hdr.ruleset));
	}
	if (trigger->log & PF_LOG_USER && !pd->lookup.done)
		pd->lookup.done = pf_socket_lookup(pd);
	if (trigger->log & PF_LOG_USER && pd->lookup.done > 0) {
		hdr.uid = pd->lookup.uid;
		hdr.pid = pd->lookup.pid;
	} else {
		hdr.uid = -1;
		hdr.pid = NO_PID;
	}
	hdr.rule_uid = rm->cuid;
	hdr.rule_pid = rm->cpid;
	hdr.dir = pd->dir;
	hdr.af = pd->af;

	if (pd->src != NULL && pd->dst != NULL) {
		if (pd->af != pd->naf ||
		    pf_addr_compare(pd->src, &pd->nsaddr, pd->naf) != 0 ||
		    pf_addr_compare(pd->dst, &pd->ndaddr, pd->naf) != 0 ||
		    pd->osport != pd->nsport ||
		    pd->odport != pd->ndport) {
			hdr.rewritten = 1;
		}
	}
	hdr.naf = pd->naf;
	pf_addrcpy(&hdr.saddr, &pd->nsaddr, pd->naf);
	pf_addrcpy(&hdr.daddr, &pd->ndaddr, pd->naf);
	hdr.sport = pd->nsport;
	hdr.dport = pd->ndport;

	ifn->if_opackets++;
	ifn->if_obytes += pd->m->m_pkthdr.len;

	bpf_mtap_hdr(if_bpf, &hdr, sizeof(hdr), pd->m, BPF_DIRECTION_OUT);
#endif

	return (0);
}
