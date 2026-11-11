#!/usr/bin/env bash
# Deterministic staging of OpenBSD 8.0 sources into module/ ports.
# Every transformation is logged in docs/porting-notes.md ("porting log").
# Re-running refreshes pristine copies while PRESERVING our port edits is
# NOT done here — ported files are committed and edited in-tree thereafter;
# this script is used (a) first-time staging, (b) `--diff <file>` to compare
# our ported copy against pristine upstream for rebase forensics.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
U="$REPO/_vendor/upstream"
M="$REPO/module"
LOG="$REPO/docs/porting-notes.md"

stage_hdrs() {
  mkdir -p "$M/include/net" "$M/include/compat/net" "$M/include/compat/sys" "$M/include/compat/crypto"
  cp -f "$U/sys/net/pfvar.h"        "$M/include/net/pfvar.h"
  cp -f "$U/sys/net/pfvar_priv.h"   "$M/include/net/pfvar_priv.h"
  cp -f "$U/sys/net/if_pflog.h"     "$M/include/compat/net/if_pflog.h"
  cp -f "$U/sys/net/radix.h"        "$M/include/compat/net/radix.h"
  cp -f "$U/sys/net/rtable.h"       "$M/include/compat/net/rtable.h"
  cp -f "$U/sys/net/ifq.h"          "$M/include/compat/net/ifq.h"
  cp -f "$U/sys/net/toeplitz.h"     "$M/include/compat/net/toeplitz.h"
  cp -f "$U/sys/sys/queue.h"        "$M/include/compat/sys/queue.h"
  cp -f "$U/sys/sys/tree.h"         "$M/include/compat/sys/tree.h"
  # NOTE: radix.h / siphash.h / toeplitz.h are NOT copied verbatim — our
  # cleaned PFLINUX versions live in module/include/compat/{net,crypto}/
  # (BSD syntax like __bounded / _BYTE_ORDER does not compile in-kernel).
}

stage_c() { # relpath upstream -> module/compat|pf
  local up="$1" dst="$2"
  cp -f "$up" "$dst"
}

# port_c <upstream-sys-rel> <module-rel> : first-time staging of an OpenBSD
# .c file with deterministic transformations (all logged to PORT notes):
#   1. all original #include lines -> single '#include <pfk.h>' (+ keep
#      none: pfvar is pulled by pfk.h chain when needed via -I)
#   2. BSD wire struct renames (avoid Linux-uapi collisions):
#      ip6_hdr→pf_ip6_hdr, struct ip→pf_ip, tcphdr→pf_tcphdr,
#      udphdr→pf_udphdr, struct icmp→pf_icmp, icmp6_hdr→icmp6hdr
#   3. BSD IFF_* flag names -> PFIFF_* (Linux owns IFF_*)
#   4. sa_len/sin_len/six_len assignment statements commented out
#   5. '#if NPFLOG > 0'-style option headers were never included (dropped)
port_c() {
  local up="$U/sys/$1" dst="$M/$2"
  [ -f "$up" ] || { echo "no upstream $1" >&2; return 1; }
  mkdir -p "$(dirname "$dst")"
  if [ -f "$dst" ]; then echo "EXISTS: $2 (not re-staged; use --diff)"; return 0; fi
  {
    printf '/* PFLINUX port of OpenBSD 8.0 %s — see docs/porting-notes.md */\n' "$1"
    printf '#include <pfk.h>\n'
    # keep everything after the last original #include (body)
    awk 'BEGIN{last=0} /^#include/{last=NR} END{print last}' "$up" |
      { read n; tail -n +$((n+1)) "$up"; }
  } > "$dst.part"
  sed -i -E \
    -e 's/\bstruct ip6_hdr\b/struct pf_ip6_hdr/g' \
    -e 's/\bstruct ip\b/struct pf_ip/g' \
    -e 's/\bstruct tcphdr\b/struct pf_tcphdr/g' \
    -e 's/\bstruct udphdr\b/struct pf_udphdr/g' \
    -e 's/\bstruct icmp\b/struct pf_icmp/g' \
    -e 's/\bstruct icmp6_hdr\b/struct icmp6hdr/g' \
    -e 's/\bIFF_([A-Z0-9_]+)/PFIFF_\1/g' \
    -e 's/\bIP_OPTIONS\b/PF_IP_OPTIONS/g' \
    -e 's/\bIP_RECORD_ROUTE\b/PF_IP_RECORD_ROUTE/g' \
    -e 's/\bIP_TIMESTAMP\b/PF_IP_TIMESTAMP/g' \
    -e 's/->sa_len[[:space:]]*=[^;]*;/\/* PFLINUX sa_len: *\/;/g' \
    -e 's/([a-z0-9_]*(sa|sin|six|sun)_len)[[:space:]]*=[^;]*;/\/* PFLINUX \1 *\/;/g' \
    "$dst.part"
  mv "$dst.part" "$dst"
  log "port_c $1 -> $2 (includes→pfk.h; struct renames; sa_len nulled)"
}

PORTED_LIST='net/pf.c net/pf_ioctl.c net/pf_norm.c net/pf_if.c net/pf_lb.c
net/pf_ruleset.c net/pf_table.c net/pf_osfp.c net/pf_syncookies.c
net/ifq.c net/hfsc.c net/fq_codel.c net/if_pflog.c netinet/ip_divert.c'
# NOTE: radix.c/rtable.c (ported by hand in T10 against our cleaned
# compat/net/radix.h) and siphash/toeplitz/sha2 (own PFLINUX impls) are
# deliberately NOT in the auto-port list.

log() { mkdir -p "$(dirname "$LOG")"; printf '\n%s\n```sh\n%s\n```\n' "## $(date -Is) staging" "$*" >> "$LOG"; }

cmd="${1:-stage}"
case "$cmd" in
  stage)
	  log "stage: headers + skeletons"
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
		port_c "$f" "pfsrc/${f##*/}";;
	      netinet/ip_divert.c)
		port_c "$f" "pfsrc/ip_divert.c";;
	    esac
	  done
	  ;;
  --diff)
	  [ -f "$U/$2" ] || { echo "no pristine $2"; exit 1; }
	  diff -u "$U/$2" "$M/$2" | head -80
	  ;;
  *)	echo "usage: stage-port.sh [stage|--diff relpath]"; exit 1;;
esac
