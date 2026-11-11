#!/usr/bin/env bash
# Fetch OpenBSD 8.0 source tarballs (primary mirror: user-provided C3SL;
# fallback: cdn.openbsd.org) and verify against VERSIONS pins.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
V="$REPO/_vendor/openbsd"; mkdir -p "$V"
MIRROR_PRIMARY="https://openbsd.c3sl.ufpr.br/pub/OpenBSD/8.0"
MIRROR_FALLBACK="https://cdn.openbsd.org/pub/OpenBSD/8.0"
declare -A WANT
while read -r f sha; do WANT[$f]=$sha; done < <(grep -E '^  (sys|src)\.tar\.gz' "$REPO/VERSIONS" | awk '{print $1, $2}')

verify() { # file expected-sha
  local f="$V/$1"
  [ -s "$f" ] && [ "$(sha256sum "$f" | cut -d' ' -f1)" = "$2" ]
}

for pair in "sys.tar.gz ${WANT[sys.tar.gz]}" "src.tar.gz ${WANT[src.tar.gz]}"; do
  set -- $pair; f=$1; sha=$2
  if verify "$f" "$sha"; then echo "OK   $f (cached, sha matches)"; continue; fi
  ok=""
  for base in "$MIRROR_PRIMARY" "$MIRROR_FALLBACK"; do
    echo "FETCH $f from $base"
    if curl -fL --retry 3 -sS -o "$V/$f.part" "$base/$f" && [ "$(sha256sum "$V/$f.part" | cut -d' ' -f1)" = "$sha" ]; then
      mv "$V/$f.part" "$V/$f"; ok=1; break
    fi
  done
  [ -n "$ok" ] || { echo "FAIL $f sha256 mismatch vs $sha (see VERSIONS)"; exit 1; }
  echo "OK   $f"
done
