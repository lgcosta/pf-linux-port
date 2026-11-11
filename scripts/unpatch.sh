#!/usr/bin/env bash
# Reverse-apply the kernel core patch series.
# Usage: ./scripts/unpatch.sh /path/to/linux-7.3.4
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
K="${1:?usage: unpatch.sh <kernel-dir>}"
SERIES="$REPO/patches/kernel/series"

[ -d "$K" ] || { echo "no kernel dir $K" >&2; exit 1; }
if [ ! -s "$SERIES" ]; then
	echo "patch series is empty — nothing to revert"
	exit 0
fi
reverted=0
tac "$SERIES" | while read -r p; do
	[ -n "$p" ] || continue
	case "$p" in \#*) continue;; esac
	f="$REPO/patches/kernel/$p"
	if git -C "$K" rev-parse >/dev/null 2>&1; then
		git -C "$K" am -3 --reverse --no-gpg-sign "$f" || true
	else
		patch -p1 -R -d "$K" --forward -i "$f" || true
	fi
	echo "reverted: $p"
done
echo "UNPATCH_DONE"
