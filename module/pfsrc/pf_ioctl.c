/* PFLINUX port of OpenBSD 8.0 net/pf_ioctl.c — see docs/porting-notes.md */
#include <pfk.h>

struct pf_statelim_kstat {
	struct kstat_kv name;
	struct kstat_kv	inuse;
	struct kstat_kv	limit;
	struct kstat_kv	admitted;
	struct kstat_kv	hardlimited;
	struct kstat_kv	ratelimited;
};

static int
pf_statelim_kstat_read(struct kstat *ks)
{
	struct pf_statelim *pfstlim = ks->ks_softc;
	struct pf_statelim_kstat *d = ks->ks_data;
	unsigned int gen;

	strlcpy(kstat_kv_istr(&d->name), pfstlim->pfstlim_nm,
	    sizeof(kstat_kv_istr(&d->name)));
	kstat_kv_u32(&d->limit) = pfstlim->pfstlim_limit;

	pc_cons_enter(&pfstlim->pfstlim_lock, &gen);
	do {
		kstat_kv_u32(&d->inuse) = pfstlim->pfstlim_inuse;
		kstat_kv_u64(&d->admitted) =
		    pfstlim->pfstlim_counters.admitted;
		kstat_kv_u64(&d->hardlimited) =
		    pfstlim->pfstlim_counters.hardlimited;
		kstat_kv_u64(&d->ratelimited) =
		    pfstlim->pfstlim_counters.ratelimited;
	} while (pc_cons_leave(&pfstlim->pfstlim_lock, &gen) != 0);

	nanouptime(&ks->ks_updated);

	return (0);
};

static void
pf_statelim_kstat_attach(struct pf_statelim *pfstlim)
{
	struct kstat *ks = pfstlim->pfstlim_ks;
	struct pf_statelim_kstat *d;

	if (ks != NULL)
		return;

	d = malloc(sizeof(*d), M_DEVBUF, M_WAITOK|M_CANFAIL|M_ZERO);
	if (d == NULL) {
		printf("pf: unable to allocate state-limiter kstat\n");
		return;
	}

	ks = kstat_create("pf", 0, "state-limiter", pfstlim->pfstlim_id,
	    KSTAT_T_KV, 0);
	if (ks == NULL) {
		printf("pf: unable to create state-limiter kstat\n");
		free(d, M_DEVBUF, sizeof(*d));
		return;
	}

	kstat_kv_init(&d->name, "name", KSTAT_KV_T_ISTR);
	kstat_kv_init(&d->inuse, "in-use", KSTAT_KV_T_UINT32);
	kstat_kv_init(&d->limit, "limit", KSTAT_KV_T_UINT32);
	kstat_kv_init(&d->admitted, "admitted", KSTAT_KV_T_COUNTER64);
	kstat_kv_init(&d->hardlimited, "hard-limited", KSTAT_KV_T_COUNTER64);
	kstat_kv_init(&d->ratelimited, "rate-limited", KSTAT_KV_T_COUNTER64);

	ks->ks_softc = pfstlim;
	ks->ks_data = d;
	ks->ks_datalen = sizeof(*d);
	ks->ks_read = pf_statelim_kstat_read;

	kstat_install(ks);

	pfstlim->pfstlim_ks = ks;
}

static void
pf_statelim_kstat_detach(struct pf_statelim *pfstlim)
{
	struct kstat *ks = pfstlim->pfstlim_ks;
	struct pf_statelim_kstat *d;

	if (ks == NULL)
		return;

	kstat_remove(ks);
	d = ks->ks_data;
	kstat_destroy(ks);

	free(d, M_DEVBUF, sizeof(*d));
}

struct pf_sourcelim_kstat {
	struct kstat_kv name;

	/* pf_source/address/prefix counters */
	struct kstat_kv	sourceinuse;
	struct kstat_kv	sourcelimit;
	struct kstat_kv	sourceallocs;
	struct kstat_kv	sourcelimited;
	struct kstat_kv	sourcenomem;

	/* state counters */
	struct kstat_kv	inuse;
	struct kstat_kv	limit;
	struct kstat_kv	admitted;
	struct kstat_kv	hardlimited;
	struct kstat_kv	ratelimited;
};

static int
pf_sourcelim_kstat_read(struct kstat *ks)
{
	struct pf_sourcelim *pfsrlim = ks->ks_softc;
	struct pf_sourcelim_kstat *d = ks->ks_data;
	unsigned int gen;

	strlcpy(kstat_kv_istr(&d->name), pfsrlim->pfsrlim_nm,
	    sizeof(kstat_kv_istr(&d->name)));
	kstat_kv_u32(&d->sourcelimit) = pfsrlim->pfsrlim_entries;
	kstat_kv_u32(&d->limit) = pfsrlim->pfsrlim_limit;

	pc_cons_enter(&pfsrlim->pfsrlim_lock, &gen);
	do {
		kstat_kv_u32(&d->sourceinuse) =
		    pfsrlim->pfsrlim_nsources;
		kstat_kv_u64(&d->sourceallocs) =
		    pfsrlim->pfsrlim_counters.addrallocs;
		kstat_kv_u64(&d->sourcelimited) =
		    pfsrlim->pfsrlim_counters.addrlimited;
		kstat_kv_u64(&d->sourcenomem) =
		    pfsrlim->pfsrlim_counters.addrnomem;

		kstat_kv_u32(&d->inuse) =
		    pfsrlim->pfsrlim_counters.inuse;
		kstat_kv_u64(&d->admitted) =
		    pfsrlim->pfsrlim_counters.admitted;
		kstat_kv_u64(&d->hardlimited) =
		    pfsrlim->pfsrlim_counters.hardlimited;
		kstat_kv_u64(&d->ratelimited) =
		    pfsrlim->pfsrlim_counters.ratelimited;
	} while (pc_cons_leave(&pfsrlim->pfsrlim_lock, &gen) != 0);

	nanouptime(&ks->ks_updated);

	return (0);
};

static void
pf_sourcelim_kstat_attach(struct pf_sourcelim *pfsrlim)
{
	struct kstat *ks = pfsrlim->pfsrlim_ks;
	struct pf_sourcelim_kstat *d;

	if (ks != NULL)
		return;

	d = malloc(sizeof(*d), M_DEVBUF, M_WAITOK|M_CANFAIL|M_ZERO);
	if (d == NULL) {
		printf("pf: unable to allocate source-limiter kstat\n");
		return;
	}

	ks = kstat_create("pf", 0, "source-limiter", pfsrlim->pfsrlim_id,
	    KSTAT_T_KV, 0);
	if (ks == NULL) {
		printf("pf: unable to create source-limiter kstat\n");
		free(d, M_DEVBUF, sizeof(*d));
		return;
	}

	kstat_kv_init(&d->name, "name", KSTAT_KV_T_ISTR);

	kstat_kv_init(&d->sourceinuse, "source-in-use", KSTAT_KV_T_UINT32);
	kstat_kv_init(&d->sourcelimit, "source-limit", KSTAT_KV_T_UINT32);
	kstat_kv_init(&d->sourceallocs, "source-allocs",
	    KSTAT_KV_T_COUNTER64);
	kstat_kv_init(&d->sourcelimited, "source-limited",
	    KSTAT_KV_T_COUNTER64);
	kstat_kv_init(&d->sourcenomem, "source-nomem",
	    KSTAT_KV_T_COUNTER64);

	kstat_kv_init(&d->inuse, "in-use", KSTAT_KV_T_UINT32);
	kstat_kv_init(&d->limit, "limit", KSTAT_KV_T_UINT32);
	kstat_kv_init(&d->admitted, "admitted", KSTAT_KV_T_COUNTER64);
	kstat_kv_init(&d->hardlimited, "hard-limited", KSTAT_KV_T_COUNTER64);
	kstat_kv_init(&d->ratelimited, "rate-limited", KSTAT_KV_T_COUNTER64);

	ks->ks_softc = pfsrlim;
	ks->ks_data = d;
	ks->ks_datalen = sizeof(*d);
	ks->ks_read = pf_sourcelim_kstat_read;

	kstat_install(ks);

	pfsrlim->pfsrlim_ks = ks;
}

static void
pf_sourcelim_kstat_detach(struct pf_sourcelim *pfsrlim)
{
	struct kstat *ks = pfsrlim->pfsrlim_ks;
	struct pf_sourcelim_kstat *d;

	if (ks == NULL)
		return;

	kstat_remove(ks);
	d = ks->ks_data;
	kstat_destroy(ks);

	free(d, M_DEVBUF, sizeof(*d));
}

#endif /* NKSTAT > 0 */
