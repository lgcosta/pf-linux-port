#!/usr/bin/env bash
# Deterministic staging of OpenBSD 8.0 sources into the module/ port.
#
# Every transformation is logged in docs/porting-notes.md ("porting log").
# Ported files are committed and edited in-tree afterwards; this script is
# used (a) for first-time staging, (b) with `--diff <file>` to compare our
# ported copy against the pristine upstream file (rebase forensics).
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
U="$REPO/_vendor/upstream"
M="$REPO/module"
LOG="$REPO/docs/porting-notes.md"

# --- shared rename program -------------------------------------------------
# BSD type/macro names that collide with Linux uapi or with the kernel's own
# macros are renamed to our pf_* equivalents.  Applied to the staged headers
# and to every ported .c file.
pf_sed() {
	sed -i -E \
	-e 's/\bstruct ip6_hdr\b/struct pf_ip6_hdr/g' \
	-e 's/\bstruct ip\b/struct pf_ip/g' \
	-e 's/\bstruct tcphdr\b/struct pf_tcphdr/g' \
	-e 's/\bstruct udphdr\b/struct pf_udphdr/g' \
	-e 's/\bstruct icmp\b/struct pf_icmp/g' \
	-e 's/\bstruct icmp6_hdr\b/struct pf_icmp6/g' \
	-e 's/\bstruct mld_hdr\b/struct pf_mld_hdr/g' \
	-e 's/\bstruct nd_neighbor_solicit\b/struct pf_nd_neighbor_solicit/g' \
	-e 's/\bstruct rtentry\b/struct pf_rtentry/g' \
	-e 's/\bstruct ifnet\b/struct pfk_ifnet/g' \
	-e 's/\bIFF_([A-Z0-9_]+)/PFIFF_\1/g' \
	-e 's/\bIP_OPTIONS\b/PF_IP_OPTIONS/g' \
	-e 's/\bIP_RECORD_ROUTE\b/PF_IP_RECORD_ROUTE/g' \
	-e 's/\bIP_TIMESTAMP\b/PF_IP_TIMESTAMP/g' \
	-e 's/ip6_fragment[(]/pfk_ip6_fragment(/g' \
	-e 's/in6_proto_cksum_out[(]/pfk_in6_proto_cksum_out(/g' \
	-e 's/ip6_forward[(]/pfk_ip6_forward(/g' \
	-e 's/\bip6_output[(]/pfk_ip6_output(/g' \
	-e 's/\bip_output[(]/pfk_ip_output(/g' \
	-e 's/\bip_forward[(]/pfk_ip_forward(/g' \
	-e 's/ifp->if_output[(]/pfk_if_output(/g' \
	-e 's/pfk_if_output[(]ifp, m, sin6tosa[(]dst[)]/pfk_if_output(ifp, m, (struct sockaddr *)sin6tosa(dst)/g' \
	-e 's/pfk_if_output[(]ifp, m, sintosa[(]dst[)]/pfk_if_output(ifp, m, (struct sockaddr *)sintosa(dst)/g' \
	-e 's/^([ \t]*)[^;]*(\.|->)(sa|sin|sin6|six|sun)[0-9]*_len[[:space:]]*=[^;]*;/\1;/' \
	-e 's/struct radix_node \[2\], u_int8_t\);/struct radix_node *, u_int8_t);/' \
	-e 's/struct pf_src_node \*\*, struct pf_test_ctx \*/struct pf_src_node *[PF_SN_MAX], struct pf_test_ctx */g' \
	-e '/^#define[[:space:]]+isupper\(c\)/d' \
	-e '/^#define[[:space:]]+islower\(c\)/d' \
	-e '/^#define[[:space:]]+isalpha\(c\)/d' \
	-e 's/\btcp_mssdflt\b/pfk_tcp_mssdflt/g' \
	-e 's/([a-z0-9_]+)->p_ucred->cr_ruid/pfk_pruid(\1)/g' \
	-e 's/([a-z0-9_]+)->p_ucred->cr_uid/pfk_pruid(\1)/g' \
	-e 's/([a-z0-9_]+)->p_p->ps_pid/pfk_ppid(\1)/g' \
	-e 's/^([a-zA-Z_][a-zA-Z0-9_ \t\*]*\b[a-zA-Z_][a-zA-Z0-9_]*)\(\);/\1(void);/' \
	"$@"
}

stage_hdrs() {
	mkdir -p "$M/include/net" "$M/include/compat/net" \
		 "$M/include/compat/sys" "$M/include/compat/crypto"
	cp -f "$U/sys/net/pfvar.h"      "$M/include/net/pfvar.h"
	cp -f "$U/sys/net/pfvar_priv.h" "$M/include/net/pfvar_priv.h"
	pf_sed "$M/include/net/pfvar.h" "$M/include/net/pfvar_priv.h"

	cp -f "$U/sys/sys/queue.h" "$M/include/compat/sys/queue.h"
	cp -f "$U/sys/sys/tree.h"  "$M/include/compat/sys/tree.h"
	# GCC requires the attribute AFTER the storage class when 'inline'
	# follows (OpenBSD writes '__unused static inline').
	sed -i 's/^__unused static inline/static inline/' \
		"$M/include/compat/sys/tree.h"

	# NOTE: radix.h / rtable.h / ifq.h / toeplitz.h / siphash.h are NOT
	# copied verbatim — our cleaned PFLINUX versions live in
	# module/include/compat/{net,crypto}/ (BSD syntax such as __bounded
	# or _BYTE_ORDER does not compile in-kernel).  Never overwrite them.
}

# port_c <upstream-sys-rel> <module-rel>
#  1. every '#include' line is removed and replaced by pfk.h + the pfvar
#     headers; all '#ifdef _KERNEL' guards stay intact (the module is built
#     with -D_KERNEL=1, so the kernel branches are compiled).
#  2. the shared rename program is applied.
port_c() {
	local up="$U/sys/$1" dst="$M/$2"
	[ -f "$up" ] || { echo "no upstream $1" >&2; return 1; }
	mkdir -p "$(dirname "$dst")"
	if [ -f "$dst" ]; then
		echo "EXISTS: $2 (not re-staged; use --diff)"
		return 0
	fi
	{
		printf '/* PFLINUX port of OpenBSD 8.0 %s — see docs/porting-notes.md */\n' "$1"
		printf '#include <pfk.h>\n'
		printf '#include <net/pfvar.h>\n'
		printf '#include <net/pfvar_priv.h>\n\n'
		sed -E '/^[[:space:]]*#include/d' "$up"
	} > "$dst.part"
	pf_sed "$dst.part"
	mv "$dst.part" "$dst"
	log "port_c $1 -> $2 (includes dropped -> pfk.h; -D_KERNEL mode; renames)"
}

PORTED_LIST='net/pf.c net/pf_ioctl.c net/pf_norm.c net/pf_if.c net/pf_lb.c
net/pf_ruleset.c net/pf_table.c net/pf_osfp.c net/pf_syncookies.c
net/ifq.c net/hfsc.c net/fq_codel.c net/if_pflog.c netinet/ip_divert.c
kern/subr_tree.c crypto/md5.c crypto/sha2.c'
# radix.c/rtable.c (hand-ported against our cleaned compat/net/radix.h) and
# siphash/toeplitz (own PFLinux implementations) are deliberately NOT in the
# automatic list.

log() {
	mkdir -p "$(dirname "$LOG")"
	printf '\n## %s staging\n\n```sh\n%s\n```\n' "$(date -Is)" "$*" >> "$LOG"
}

cmd="${1:-stage}"
case "$cmd" in
stage)
	log "stage: headers"
	stage_hdrs
	echo "headers staged: $(find "$M/include/net" "$M/include/compat" -type f | wc -l)"
	;;
port)
	shift
	for f in ${*:-$PORTED_LIST}; do
		case "$f" in
		net/pf.c|net/pf_ioctl.c|net/pf_norm.c|net/pf_if.c|net/pf_lb.c|\
		net/pf_ruleset.c|net/pf_table.c|net/pf_osfp.c|net/pf_syncookies.c|\
		net/ifq.c|net/hfsc.c|net/fq_codel.c|net/if_pflog.c)
			port_c "$f" "pfsrc/${f##*/}" ;;
		net/radix.c|net/rtable.c)
			port_c "$f" "compat/${f##*/}" ;;
		kern/subr_tree.c)
			port_c "$f" "compat/subr_tree.c" ;;
		netinet/ip_divert.c)
			port_c "$f" "pfsrc/ip_divert.c" ;;
		crypto/md5.c|crypto/sha2.c)
			port_c "$f" "compat/${f##*/}" ;;
		esac
	done
	;;
--diff)
	[ -f "$U/$2" ] || { echo "no pristine $2"; exit 1; }
	diff -u "$U/$2" "$M/$2" | head -80
	;;
*)
	echo "usage: stage-port.sh [stage|port [files...]|--diff relpath]"
	exit 1
	;;
esac
