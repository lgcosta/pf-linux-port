#!/usr/bin/env bash
# Stage pristine OpenBSD 8.0 sources used by port tasks into _vendor/upstream.
# Run after fetch-sources.sh. Output tree is gitignored; IDS.txt records each
# file's $OpenBSD: tag for rebase forensics.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
V="$REPO/_vendor/openbsd"; U="$REPO/_vendor/upstream"
SYS_K=(net/pf.c net/pf_ioctl.c net/pf_norm.c net/pf_if.c net/pf_lb.c net/pf_ruleset.c
       net/pf_table.c net/pf_osfp.c net/pf_syncookies.c net/pfvar.h net/pfvar_priv.h
       net/if_pflog.c net/if_pflog.h net/ifq.c net/ifq.h net/hfsc.c net/hfsc.h
       net/fq_codel.c net/fq_codel.h netinet/ip_divert.c netinet/ip_divert.h
       net/radix.c net/radix.h net/rtable.c net/rtable.h net/toeplitz.c net/toeplitz.h
       crypto/md5.c crypto/md5.h crypto/sha2.c crypto/sha2.h crypto/siphash.c crypto/siphash.h
       sys/tree.h sys/queue.h sys/param.h sys/hash.h sys/_endian.h sys/_lock.h)
SRC_K=(sbin/pfctl/parse.y sbin/pfctl/pfctl.c sbin/pfctl/pfctl.h sbin/pfctl/pfctl_parser.c
       sbin/pfctl/pfctl_parser.h sbin/pfctl/pfctl_optimize.c sbin/pfctl/pfctl_print.c
       sbin/pfctl/pfctl_osfp.c sbin/pfctl/pfctl_queue.c sbin/pfctl/pfctl_radix.c
       sbin/pfctl/pfctl_table.c sbin/pfctl/pfctl_snmp.c sbin/pfctl/pf_print_state.c
       sbin/pfctl/pfctl.8 include/net/pfvar.h include/net/if_pflog.h)

rm -rf "$U"; mkdir -p "$U/sys/net" "$U/sys/sys" "$U/sys/netinet" "$U/sbin/pfctl" "$U/include/net"
for f in "${SYS_K[@]}"; do
  if ! tar xzf "$V/sys.tar.gz" -C "$U" "./sys/$f" "sys/$f" 2>/dev/null; then
    echo "WARN: sys/$f not found in 8.0-sys" >&2
  fi
done
for f in "${SRC_K[@]}"; do
  tar xzf "$V/src.tar.gz" -C "$U" "./$f" "$f" 2>/dev/null || echo "WARN: $f not found in 8.0-src" >&2
done
find "$U" -type f \( -name '*.c' -o -name '*.h' -o -name '*.y' -o -name '*.8' \) | sort | while read -r p; do
  tag=$(grep -m1 -oE '\$OpenBSD: [^$]*\$' "$p" | head -1 | sed 's/\$OpenBSD: //;s/ \$//')
  echo "${p#$U/} :: ${tag:-NO-ID}"
done > "$U/IDS.txt"
n=$(find "$U" -type f \( -name '*.c' -o -name '*.h' -o -name '*.y' \) | wc -l)
echo "staged $n files -> $U (IDS.txt)"
[ "$n" -ge 30 ]
