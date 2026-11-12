#!/usr/bin/env bash
# M7 gate: stress — the PF engine under sustained load.
#
# Unlike the functional gates (01-15), this one does not ask "does the packet
# arrive?"; it asks "does the engine stay correct, bounded and leak-free while
# traffic, state, rules and tables churn?"  Phases:
#
#   0 baseline    throughput cli->srv at a bounded rate with the module OUT
#                 of the path
#   1 overhead    same traffic with PF enabled + stateful rules
#   2 state churn 300 concurrent TCP connections; states must appear and
#                 must vanish again (pfctl -F s) -> no state-table leak
#   3 rule churn  200 reloads of a 200-rule ruleset, timed on the firewall
#   4 table churn 2000 pfr table entries added and flushed
#   5 integrity   2 MB through NAT *while* a concurrent stream runs; the
#                 payload must arrive byte-identical (sha256)
#   6 health      no BUG/WARNING/Oops, counters move, firewall still routes
#
# Every phase prints `STRESS key=value ...` lines; scripts/stress/run-stress.sh
# turns them into a report.  Thresholds are overridable from the environment
# (STRESS_SECS, STRESS_CONNS, STRESS_RELOADS, STRESS_TABLE_ENTRIES, ...).
set -uo pipefail; cd "$(dirname "$0")"; . ./lib.sh

SECS=${STRESS_SECS:-10}
PAR=${STRESS_PARALLEL:-4}
RATE=${STRESS_RATE:-100M}
CONNS=${STRESS_CONNS:-300}
RELOADS=${STRESS_RELOADS:-200}
RULES=${STRESS_RULES:-200}
TENTRIES=${STRESS_TABLE_ENTRIES:-2000}
HOLD=${STRESS_HOLD:-6}
MAXOH=${STRESS_MAX_OVERHEAD_PCT:-30}

# The traffic phases run at a BOUNDED rate ($RATE per stream).  This is not
# cosmetic: PF's data path steals every skb onto one ordered workqueue
# (see docs/*/caveats.md §1), so the backlog — and with it the guest's
# memory — grows with the offered rate.  Measured on the lab (1 GB guest):
# 400 Mbit/s is stable, ~1 Gbit/s OOM-panics the guest.  The stress test
# therefore works in the regime the port survives and reports the ceiling
# separately, instead of killing the firewall under test.

HELPER=$(pfput ./stress-helper.sh) || { bad "helper upload"; finish; }
pf0 "chmod +x $HELPER" >/dev/null

EGRESS_DEV=$(pf0 "ip route get $SRV_IP | grep -o 'dev [a-z0-9]*' | cut -d' ' -f2")
[ -n "$EGRESS_DEV" ] || EGRESS_DEV=eth2
EGRESS_IP=$(pf0 "ip -4 addr show $EGRESS_DEV | grep -o 'inet [0-9.]*' | head -1 | cut -d' ' -f2")
[ -n "$EGRESS_IP" ] || EGRESS_IP=192.168.122.8

# --- helpers ---------------------------------------------------------------

# iperf3 throughput cli->srv, in Mbit/s (one decimal is plenty)
iperf_mbit() {
	local out line val unit
	out=$(cli "iperf3 -c $SRV_IP -t $1 -P $PAR -b $RATE -i 0 2>/dev/null")
	line=$(printf '%s\n' "$out" | grep '\[SUM\]' | tail -1)
	[ -n "$line" ] || { echo 0; return; }
	val=$(printf '%s\n' "$line" | sed -n 's/.*[^0-9.]\([0-9][0-9.]*\) \([KMG]\?bits\/sec\).*/\1/p')
	unit=$(printf '%s\n' "$line" | sed -n 's/.* \([KMG]\?bits\/sec\).*/\1/p')
	[ -n "$val" ] || { echo 0; return; }
	awk -v v="$val" -v u="$unit" 'BEGIN{
		if (u ~ /^G/) printf "%.1f\n", v * 1000;
		else if (u ~ /^M/) printf "%.1f\n", v;
		else if (u ~ /^K/) printf "%.1f\n", v / 1000;
		else printf "%.3f\n", v / 1000000; }'
}
# `pkill -x iperf3` matches the process *name* only.  Never use `pkill -f`
# with a pattern that also appears in this very command line: it kills the
# invoking shell (see the note in 06-rdr.sh).
iperf_up() { srv 'pkill -x iperf3 2>/dev/null; iperf3 -s -D >/dev/null 2>&1; sleep 1; pgrep -x iperf3 >/dev/null || echo IPERF_SERVER_DOWN' >/dev/null; }

# --- phase 0: baseline (module out of the path) ----------------------------
pf0 'rmmod pf 2>/dev/null; true' >/dev/null
iperf_up
BASE=$(iperf_mbit "$SECS")
BASE=$(printf '%s' "$BASE" | cut -d. -f1)
if [ "${BASE:-0}" -gt 0 ]; then
	ok "baseline throughput ${BASE} Mbit/s (module unloaded)"
else
	bad "baseline throughput unavailable (iperf3 cli->srv failed)"
	echo "STRESS phase=baseline baseline_mbit=0 seconds=$SECS parallel=$PAR rate_per_stream=$RATE"
	finish
fi
echo "STRESS phase=baseline baseline_mbit=$BASE seconds=$SECS parallel=$PAR rate_per_stream=$RATE"

# --- phase 1: same traffic with PF enabled ---------------------------------
pf0 'insmod /opt/pf/pf.ko 2>/dev/null || insmod /tmp/pf.ko; true' >/dev/null
pf0 '/opt/pf/pfctl -e; /opt/pf/pfctl -f /dev/null' >/dev/null

cat > /tmp/g16.conf <<C
pass in quick proto tcp from $CLI_IP to $SRV_IP keep state
pass in quick proto icmp from $CLI_IP keep state
pass out quick keep state
C
pfload /tmp/g16.conf || { bad "stress ruleset"; finish; }

iperf_up
PFM=$(iperf_mbit "$SECS")
PFM=$(printf '%s' "$PFM" | cut -d. -f1)
[ "${PFM:-0}" -gt 0 ] && ok "throughput with PF enabled: ${PFM} Mbit/s" || bad "no throughput with PF enabled"
OH=0
[ "${BASE:-0}" -gt 0 ] && OH=$(awk -v b="$BASE" -v p="$PFM" 'BEGIN{printf "%d", (b-p)*100/b}')
if [ "${OH:-999}" -le "$MAXOH" ]; then
	ok "PF overhead ${OH}% (<= ${MAXOH}%)"
else
	bad "PF overhead ${OH}% exceeds ${MAXOH}%"
fi
echo "STRESS phase=overhead pf_mbit=$PFM overhead_pct=$OH limit_pct=$MAXOH rate_per_stream=$RATE"

# --- phase 2: state churn ---------------------------------------------------
# The churn client cannot use sshd: MaxStartups (10:30:100 by default) drops
# most concurrent unauthenticated connections.  The server therefore gets a
# dedicated listener (python's http.server forks per connection and holds it
# open until the client closes).
#
# The setup and cleanup scripts are shipped base64-encoded: a plain `$(...)`
# would be expanded by the *intermediate* host (deb), not by the guest, and
# would silently do nothing.  The listener also outlives the ssh session, so
# the setup kills leftovers from an earlier run (an orphan holding the port
# would silently break the churn).
SETUP=$(cat <<'EOS' | base64 | tr -d '\n'
mkdir -p /tmp/stress-web
cd /tmp/stress-web || exit 0
echo ok > index.html
for pid in $(ls /proc 2>/dev/null | grep -E '^[0-9]+$'); do
  [ "$pid" = "$$" ] && continue
  cmd=$(tr '\0' ' ' < /proc/$pid/cmdline 2>/dev/null) || continue
  case "$cmd" in
    *stress-loop*|*http.server*) kill "$pid" 2>/dev/null ;;
  esac
done
if [ -f /tmp/stress-httpd.pid ]; then kill $(cat /tmp/stress-httpd.pid) 2>/dev/null; rm -f /tmp/stress-httpd.pid; fi
setsid sh -c 'echo $$ > /tmp/stress-httpd.pid; exec python3 -m http.server 9097 --bind 0.0.0.0' >/dev/null 2>&1 </dev/null &
sleep 1
EOS
)
srv "echo $SETUP | base64 -d > /tmp/stress-setup.sh; sh /tmp/stress-setup.sh" >/dev/null
if srv 'netstat -lnt 2>/dev/null | grep -q ":9097 "' >/dev/null 2>&1; then
	ok "churn listener up on srv:9097"
else
	bad "churn listener NOT listening on srv:9097 (stale listener holding the port?)"
	finish
fi
RULESET=$(pf0 '/opt/pf/pfctl -s rules | wc -l')
PFSTAT=$(pf0 '/opt/pf/pfctl -s info | head -1')
pf0 "/opt/pf/pfctl -F s" >/dev/null
S0=$(pf0 "$HELPER states")
# The connections must stay open while the states are counted: a peer that
# closes right away tears the state down before it can be observed.
# The iteration list is expanded HERE, on the build host: a `$(seq ...)`
# left in the command string would be expanded by the *intermediate* host
# (deb), whose newline-separated output breaks the `for` loop one hop later.
RANGE=$(seq -s' ' 1 "$CONNS")
cli "bash -c 'for i in $RANGE; do (exec 3<>/dev/tcp/$SRV_IP/9097; sleep $HOLD) 2>/dev/null & done; wait'" >/dev/null 2>&1 &
CLIPID=$!
sleep 4
S1=$(pf0 "$HELPER states")
EST=$(srv 'netstat -tn 2>/dev/null | grep -c ":9097 "')
wait $CLIPID 2>/dev/null
if [ "${S1:-0}" -ge $((CONNS / 2)) ]; then
	ok "state churn: $S1 states tracked for $CONNS connections"
else
	bad "state churn: only $S1 states for $CONNS connections"
fi
S2=$(pf0 "$HELPER state-reset")
sleep 1
S3=$(pf0 "$HELPER states")
if [ "${S3:-0}" -le "${S0:-0}" ]; then
	ok "state table drained after reset ($S0 -> $S3)"
else
	bad "state table did not drain ($S0 -> $S3)"
fi
echo "STRESS phase=state_churn connections=$CONNS states_before=$S0 states_created=$S1 states_after_reset=$S3 established_on_srv=${EST:-0} rules_loaded=$RULESET pf_status=$(printf '%s' "$PFSTAT" | tr -s ' ' | cut -d' ' -f2-)"

# --- phase 3: rule churn ---------------------------------------------------
{
	echo "table <stress> { }"
	echo "pass in quick proto tcp from $CLI_IP to $SRV_IP keep state"
	echo "pass out quick keep state"
	i=1
	while [ "$i" -le "$RULES" ]; do
		# a distinct destination port per rule: rules with the same shape
		# are folded by pfctl's optimizer into one automatic table, which
		# would make the rule count meaningless
		echo "block drop in quick proto tcp from 10.100.$((i % 250)).$((i % 250 + 1)) to any port $((1000 + i))"
		i=$((i + 1))
	done
} > /tmp/g16-churn.conf
CH=$(pfput /tmp/g16-churn.conf) || { bad "churn ruleset upload"; finish; }
pf0 "/opt/pf/pfctl -f $CH" >/dev/null || { bad "churn ruleset load"; finish; }
R0=$(pf0 "$HELPER rules")
RL=$(pf0 "$HELPER reload-loop $RELOADS $CH")
eval "$RL"
if [ "${reload_fails:-1}" -eq 0 ]; then
	ok "rule churn: $RELOADS reloads, no failure (avg ${reload_ms_avg:-?} ms, max ${reload_ms_max:-?} ms)"
else
	bad "rule churn: ${reload_fails:-?} of $RELOADS reloads failed"
fi
R1=$(pf0 "$HELPER rules")
if [ "${R1:-0}" -eq "${R0:-0}" ] && [ "${R1:-0}" -eq "$((RULES + 2))" ]; then
	ok "rule count stable across churn ($R1 rules)"
else
	bad "rule count drifted: $R0 -> $R1 (expected $((RULES + 2)))"
fi
echo "STRESS phase=rule_churn reloads=$RELOADS rules_per_load=$RULES reload_fails=${reload_fails:-0} reload_ms_avg=${reload_ms_avg:-0} reload_ms_max=${reload_ms_max:-0} rules_final=$R1"

# --- phase 4: table churn --------------------------------------------------
TC=$(pf0 "$HELPER table-churn $TENTRIES stress")
eval "$TC"
if [ "${table_add_errors:-1}" -eq 0 ] && [ "${table_entries_after_add:-0}" -ge $((TENTRIES / 2)) ]; then
	ok "table churn: ${table_entries_after_add} entries added"
else
	bad "table churn: ${table_entries_after_add:-0} entries, ${table_add_errors:-?} errors"
fi
if [ "${table_entries_after_flush:-1}" -eq 0 ]; then
	ok "table flushed back to 0 entries"
else
	bad "table not empty after flush (${table_entries_after_flush})"
fi
echo "STRESS phase=table_churn entries_requested=$TENTRIES entries_added=${table_entries_after_add:-0} add_errors=${table_add_errors:-0} entries_after_flush=${table_entries_after_flush:-0}"

# --- phase 5: integrity under load -----------------------------------------
cat > /tmp/g16-nat.conf <<C
pass out quick on $EGRESS_DEV from $CLI_IP to any nat-to ($EGRESS_DEV)
pass in quick proto tcp from $CLI_IP to $SRV_IP port 9099 keep state
pass out quick keep state
C
pfload /tmp/g16-nat.conf || { bad "nat ruleset"; finish; }

SZ=2000000
srv 'pkill nc 2>/dev/null; head -c 2000000 /dev/zero | tr "\0" A > /tmp/stress.bin; sleep 1; nohup setsid nc -l -p 9099 < /tmp/stress.bin >/dev/null 2>&1 & sleep 1' >/dev/null
SRC_SUM=$(srv 'sha256sum /tmp/stress.bin | cut -c1-64' | tr -d '[:space:]')
# saturating background stream while the payload crosses PF
cli "iperf3 -c $SRV_IP -t 15 -P $PAR -b $RATE >/dev/null 2>&1 &" >/dev/null 2>&1
sleep 1
GOT=$(cli 'nc -w20 10.0.2.20 9099 | sha256sum | cut -c1-64' | tr -d '[:space:]')
if [ -n "$SRC_SUM" ] && [ "$GOT" = "$SRC_SUM" ]; then
	ok "2 MB through NAT intact under load (sha256 match)"
	INTEG=yes
else
	bad "payload corrupted under load (src=$SRC_SUM got=$GOT)"
	INTEG=no
fi
echo "STRESS phase=integrity bytes=$SZ sha256_match=$INTEG"

# --- phase 6: health -------------------------------------------------------
ANOM=$(pf0 "$HELPER dmesg")
if [ -z "$ANOM" ]; then
	ok "no kernel BUG/WARN/Oops after the stress run"
else
	bad "dmesg anomalies:"
	printf '%s\n' "$ANOM" | sed 's/^/    /'
fi
MATCHED=$(pf0 "$HELPER counter match")
ENTRIES=$(pf0 "$HELPER state-entries")
[ "${MATCHED:-0}" -gt 0 ] && ok "PF counters moved (match=${MATCHED:-0} state_entries=${ENTRIES:-0})" \
	|| bad "PF counters did not move (match=${MATCHED:-0})"
cli "ping -c 3 -W 2 $SRV_IP >/dev/null 2>&1" && ok "firewall still routes traffic after stress" \
	|| bad "firewall stopped routing traffic after stress"
echo "STRESS phase=health dmesg_anomalies=$(printf '%s' "$ANOM" | grep -c . ) match=${MATCHED:-0} state_entries=${ENTRIES:-0}"

# --- cleanup ---------------------------------------------------------------
CLEAN=$(cat <<'EOS' | base64 | tr -d '\n'
if [ -f /tmp/stress-httpd.pid ]; then kill $(cat /tmp/stress-httpd.pid) 2>/dev/null; rm -f /tmp/stress-httpd.pid; fi
for pid in $(ls /proc 2>/dev/null | grep -E '^[0-9]+$'); do
  [ "$pid" = "$$" ] && continue
  cmd=$(tr '\0' ' ' < /proc/$pid/cmdline 2>/dev/null) || continue
  case "$cmd" in
    *http.server*) kill "$pid" 2>/dev/null ;;
  esac
done
pkill -x nc 2>/dev/null
pkill -x iperf3 2>/dev/null
true
EOS
)
srv "echo $CLEAN | base64 -d > /tmp/stress-clean.sh; sh /tmp/stress-clean.sh" >/dev/null
cli 'pkill -x iperf3 2>/dev/null; true' >/dev/null
pf0 '/opt/pf/pfctl -d; /opt/pf/pfctl -f /dev/null' >/dev/null
finish
