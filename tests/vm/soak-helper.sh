#!/usr/bin/env bash
# Helper executed ON pf0 by 14-soak.sh (uploaded with pfput to avoid quoting
# hell through two ssh hops).
#   $1 = "slab" | "dmesg" | "states"
case "${1:-}" in
slab)
	grep -E '^(pf|pfk)' /proc/slabinfo | tr -s ' ' | cut -d' ' -f1,3 | sort
	;;
dmesg)
	dmesg | grep -E 'BUG:|WARNING:|Oops|general protection|kernel BUG' || true
	;;
states)
	/opt/pf/pfctl -s states 2>/dev/null | grep -c 'icmp' || true
	;;
esac
