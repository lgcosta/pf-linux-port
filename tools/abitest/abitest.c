/*
 * PFLINUX ABI table (acceptance item 3).
 *
 * Computes sizeof()/offsetof() of the key pf structs shared between this
 * userland and pf.ko, compiled against tools/include (or, when built as
 * out/abitest-module, against ../module/include/net/pfvar.h directly).
 * Prints a deterministic table plus an MD5 digest of it; equal digests
 * between the two builds prove the userland copy and the kernel-side
 * staging header agree.  The kernel-internal side of the comparison
 * (structs compiled with the module compat headers under -D_KERNEL) is
 * performed separately by the integrator.
 *
 * Note: OpenBSD 8.0 moved struct pf_state_key into <net/pfvar_priv.h>
 * (kernel-only).  The userland-visible mirror is struct
 * pf_state_key_cmp ("keep synced with struct pf_state_key, used in
 * RB_FIND") plus struct pfsync_state_key; both are tabulated below.
 */

#include <sys/types.h>
#include <net/if.h>
#include <net/pfvar.h>

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <md5.h>

_Static_assert(sizeof(struct pf_addr) == 16, "pf_addr must be 16 bytes");
_Static_assert(sizeof(((struct pf_addr *)NULL)->addr32) == 16,
    "pf_addr union must be 16 bytes");
_Static_assert(sizeof(((struct pf_status *)NULL)->ifname) == IFNAMSIZ,
    "pf_status.ifname sized by IFNAMSIZ");
_Static_assert(PF_MD5_DIGEST_LENGTH == MD5_DIGEST_LENGTH,
    "pfvar.h and md5.h digest length agree");

struct probe {
	const char *name;
	size_t	size;
	size_t	off1;
	size_t	off2;
	size_t	off3;
};

#define P(st, a)	{ #st "." #a, sizeof(struct st), \
				offsetof(struct st, a), \
				offsetof(struct st, a), \
				offsetof(struct st, a) }
#define P3(st, a, b, c)	{ #st "." #a "/" #b "/" #c, sizeof(struct st), \
				offsetof(struct st, a), \
				offsetof(struct st, b), \
				offsetof(struct st, c) }

static const struct probe probes[] = {
	/* struct, off1, off2, off3 */
	P3(pf_addr,		pfa,	pfa,	pfa),
	P3(pf_status,		counters, hostid, pf_chksum),
	P3(pf_state_key_cmp,	addr,	port,	rdomain),
	P3(pfsync_state_key,	addr,	port,	rdomain),
	P3(pfsync_state,	id,	ifname,	key),
	P3(pfioc_state,		state, state, state),
	P3(pf_rule,		src,	ifname,	rtableid),
	P3(pf_rule,		nat,	rdr,	route),
	P3(pf_rule,		evaluations, anchor, timeout),
	P3(pfioc_rule,		action,	anchor,	rule),
	P3(pf_rule_addr,	addr,	neg,	port_op),
	P3(pf_addr_wrap,	v,	type,	iflags),
	P3(pf_pool,		addr,	key,	states),
	P3(pf_queuespec,	qname,	ifname,	flowqueue),
	P3(pfioc_queue,		ticket,	nr,	queue),
	P3(pfi_kif,		pfik_name, pfik_packets, pfik_states),
	P3(pfr_tstats,		pfrts_t, pfrts_match, pfrts_cnt),
	P3(pfr_addr,		pfra_u,	pfra_af,	pfra_net),
	P3(pfioc_table,		pfrio_table, pfrio_buffer, pfrio_esize),
	P3(pfioc_trans_e,	type,	anchor,	ticket),
	P3(pf_osfp_ioctl,	fp_os,	fp_wsize,	fp_psize),
	P3(pfr_kcounters,	pfrkc_packets, pfrkc_bytes, states),
};

int
main(void)
{
	char buf[4096];
	size_t n = 0;
	MD5_CTX mc;
	u_int8_t dg[MD5_DIGEST_LENGTH];
	int i;

	printf("%-24s %8s %8s %8s %8s\n",
	    "type.member", "size", "off1", "off2", "off3");
	printf("----------------------------------------"
	    "-----------------------------\n");
	for (i = 0; i < (int)(sizeof(probes)/sizeof(probes[0])); i++) {
		char line[256];
		int len;

		len = snprintf(line, sizeof(line), "%-24s %8zu %8zu %8zu %8zu\n",
		    probes[i].name, probes[i].size, probes[i].off1,
		    probes[i].off2, probes[i].off3);
		if (len < 0 || (size_t)len >= sizeof(line)) {
			fprintf(stderr, "line too long\n");
			return (1);
		}
		fputs(line, stdout);
		if (n + strlen(line) >= sizeof(buf)) {
			fprintf(stderr, "table buffer overflow\n");
			return (1);
		}
		memcpy(buf + n, line, strlen(line));
		n += strlen(line);
	}

	printf("----------------------------------------"
	    "-----------------------------\n");
	printf("%d entries\n", (int)(sizeof(probes)/sizeof(probes[0])));

	/* MD5 of just the table body (deterministic across both builds) */
	MD5Init(&mc);
	MD5Update(&mc, buf, n);
	MD5Final(dg, &mc);
	printf("md5 ");
	for (i = 0; i < MD5_DIGEST_LENGTH; i++)
		printf("%02x", dg[i]);
	printf("\n");
	return (0);
}
