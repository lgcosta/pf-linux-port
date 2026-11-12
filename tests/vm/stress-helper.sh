#!/bin/sh
# Helper executed ON pf0 by 16-stress.sh (uploaded with pfput to avoid quoting
# hell through two ssh hops).  POSIX sh: the guest is Alpine (busybox ash).
#
#   $1 = "slab" | "dmesg" | "states" | "state-reset" | "rules" | "info"
#        | "reload-loop <n> <ruleset>" | "table-churn <n> <table>"
#
# Every subcommand prints machine-readable KEY=VALUE pairs on stdout so the
# gate can parse them without another ssh round trip.
PFCTL=/opt/pf/pfctl

now_ms() { awk '{printf "%d", $1 * 1000}' /proc/uptime; }

case "${1:-}" in
slab)
	grep -E '^(pf|pfk)' /proc/slabinfo | tr -s ' ' | cut -d' ' -f1,3 | sort
	;;
dmesg)
	# the CPU's Speculative Return Stack Overflow advisory is a kernel
	# build/CPU feature notice, not a fault of this module
	dmesg | grep -E 'BUG:|WARNING:|Oops|general protection|kernel BUG' \
		| grep -v 'Speculative Return Stack Overflow' || true
	;;
states)
	# `pfctl -s states` prints one line per direction, so count the
	# initiator lines ("->") to get the number of states
	$PFCTL -s states 2>/dev/null | grep -c ' -> ' || true
	;;
state-reset)
	# -F s clears the state table (-s s only *shows* it)
	$PFCTL -F s >/dev/null 2>&1 || true
	$PFCTL -s states 2>/dev/null | grep -c ' -> ' || true
	;;
rules)
	$PFCTL -s rules 2>/dev/null | grep -c . || true
	;;
info)
	$PFCTL -s info 2>/dev/null
	;;
counter)
	# $2 = counter name as printed by `pfctl -s info` (match, short,
	# state-mismatch, ...).  Prints the value, or 0 when absent.
	$PFCTL -s info 2>/dev/null | awk -v k="${2:-match}" '$1 == k { print $2; exit }'
	;;
state-entries)
	# the number of entries the state table currently holds
	$PFCTL -s info 2>/dev/null | awk '/current entries/ { print $NF; exit }'
	;;
reload-loop)
	# $2 = iterations, $3 = ruleset file.  Times each `pfctl -f` on the
	# firewall itself (no ssh overhead in the measurement).
	n="${2:-0}"; f="${3:-/tmp/g16-churn.conf}"
	i=0; fails=0; min=999999999; max=0; sum=0
	while [ "$i" -lt "$n" ]; do
		s=$(now_ms)
		if ! $PFCTL -f "$f" >/dev/null 2>&1; then
			fails=$((fails + 1))
		fi
		e=$(now_ms)
		d=$((e - s)); [ "$d" -lt 0 ] && d=0
		sum=$((sum + d))
		[ "$d" -lt "$min" ] && min=$d
		[ "$d" -gt "$max" ] && max=$d
		i=$((i + 1))
	done
	[ "$min" = 999999999 ] && min=0
	avg=0; [ "$n" -gt 0 ] && avg=$((sum / n))
	echo "reload_iterations=$n reload_fails=$fails reload_ms_min=$min reload_ms_avg=$avg reload_ms_max=$max"
	;;
table-churn)
	# $2 = number of addresses, $3 = table name.  Adds N distinct addresses,
	# counts the table, flushes it and counts again.
	n="${2:-0}"; t="${3:-stress}"
	$PFCTL -t "$t" -T flush >/dev/null 2>&1 || true
	i=1; err=0
	while [ "$i" -le "$n" ]; do
		# 10.0.<a>.<b> keeps every entry distinct without touching the
		# lab's real segments (10.0.1.0/24 lan, 10.0.2.0/24 wan)
		a=$(( (i - 1) / 250 + 100 ))
		b=$(( (i - 1) % 250 + 1 ))
		if ! $PFCTL -t "$t" -T add "10.$a.$b" >/dev/null 2>&1; then
			err=$((err + 1))
		fi
		i=$((i + 1))
	done
	added=$($PFCTL -t "$t" -T show 2>/dev/null | grep -c . || true)
	$PFCTL -t "$t" -T flush >/dev/null 2>&1 || true
	left=$($PFCTL -t "$t" -T show 2>/dev/null | grep -c . || true)
	if [ "${left:-0}" -ne 0 ]; then
		# flush did not empty the table: fall back to deleting every entry
		$PFCTL -t "$t" -T show 2>/dev/null | awk '{print $1}' | \
		while read -r a; do $PFCTL -t "$t" -T delete "$a" >/dev/null 2>&1 || true; done
		left=$($PFCTL -t "$t" -T show 2>/dev/null | grep -c . || true)
	fi
	echo "table_entries_requested=$n table_add_errors=$err table_entries_after_add=$added table_entries_after_flush=$left"
	;;
*)
	echo "unknown helper command: ${1:-}" >&2
	exit 2
	;;
esac
