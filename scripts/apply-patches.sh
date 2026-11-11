#!/usr/bin/env bash
# Apply (or verify) the kernel core patch series against a Linux tree.
# Usage: ./scripts/apply-patches.sh /path/to/linux-7.3.4
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
K="${1:?usage: apply-patches.sh <kernel-dir>}"
SERIES="$REPO/patches/kernel/series"

[ -d "$K" ] || { echo "no kernel dir $K" >&2; exit 1; }
if [ ! -s "$SERIES" ]; then
	echo "patch series is empty (none needed) — nothing to apply"
	# still ensure build prep happened (idempotent hint)
	[ -f "$K/Module.symvers" ] || echo "note: run scripts/build-kernel.sh first"
	exit 0
fi
applied=0
while read -r p; do
	[ -n "$p" ] || continue
	case "$p" in \#*) continue;; esac
	f="$REPO/patches/kernel/$p"
	[ -f "$f" ] || { echo "missing $f" >&2; exit 1; }
	if git -C "$K" rev-parse >/dev/null 2>&1; then
		git -C "$K" am -3 --no-gpg-sign "$f" && applied=$((applied+1))
	else
		patch -p1 -d "$K" --forward -i "$f" && applied=$((applied+1))
	fi
	echo "applied: $p  (see docs/*/patches.md)"
done < "$SERIES"
echo "PATCHES_APPLIED=$applied"
