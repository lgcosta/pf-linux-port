#!/usr/bin/env bash
# bench/backend.sh — load/unload one backend's ruleset on the firewall VM.
# Usage: backend.sh <backend> <ruleset-file>   (backend: pf|nft|ipt-nft|ipt-legacy|none)
set -uo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/env.sh"

BACKEND="${1:-}"; RULES="${2:-}"
[ -n "$BACKEND" ] || die "backend.sh: backend required"

apply() {
	case "$BACKEND" in
	pf)
		scp -q $VBASE[*] "$RULES" "$FW:/tmp/bench-pf.conf"
		on_fw "/usr/local/sbin/pfctl -d; /usr/local/sbin/pfctl -F all >/dev/null; /usr/local/sbin/pfctl -e; /usr/local/sbin/pfctl -f /tmp/bench-pf.conf"
		;;
	nft)
		scp -q $VBASE[*] "$RULES" "$FW:/tmp/bench-nft.rules"
		on_fw "nft -f /tmp/bench-nft.rules"
		;;
	ipt-nft)
		scp -q $VBASE[*] "$RULES" "$FW:/tmp/bench-ipt.rules"
		on_fw "iptables-nft-restore < /tmp/bench-ipt.rules"
		;;
	ipt-legacy)
		scp -q $VBASE[*] "$RULES" "$FW:/tmp/bench-ipt.rules"
		on_fw "iptables-legacy-restore < /tmp/bench-ipt.rules"
		;;
	none)
		on_fw "set +e; nft flush ruleset 2>/dev/null; iptables-nft-restore < /dev/null 2>/dev/null; true"
		;;
	*) die "backend.sh: unknown backend $BACKEND" ;;
	esac
}

apply "$@"
