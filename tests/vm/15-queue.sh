#!/usr/bin/env bash
# M5 gate: hfsc queue in the data path.
#
# What is verifiable with OpenBSD 8.0's pf: a queue configuration loads, the
# hfsc discipline attaches to the interface, `pfctl -s queue -v` reports the
# class statistics, a queued flow completes through the class, and that class's
# packet/byte counters advance - i.e. the scheduler really sits in the path.
#
# What is NOT a cap here: 8.0's pf.conf exposes only `bandwidth` (HFSC
# linkshare).  There is no `minimum`/`maximum` keyword in this vintage, and
# linkshare is a guarantee under contention, not a limit on a lone flow - the
# same on OpenBSD.  Timing is printed for information, not asserted.
set -uo pipefail; cd "$(dirname "$0")"; . ./lib.sh

SZ=2000000
IF_GW=$(pf0 "ip route get $SRV_IP | grep -o 'dev [a-z0-9]*' | cut -d' ' -f2")
[ -n "$IF_GW" ] || IF_GW=eth2

pf0 'insmod /opt/pf/pf.ko 2>/dev/null || insmod /tmp/pf.ko; true'
pf0 '/opt/pf/pfctl -e; /opt/pf/pfctl -f /dev/null' >/dev/null

cat > /tmp/g15.conf <<C
queue rootq on $IF_GW bandwidth 10Mbit
queue leafq on $IF_GW bandwidth 2Mbit parent rootq default
pass in quick proto tcp from $CLI_IP to $SRV_IP port 9095 queue leafq keep state
pass out quick keep state
C
pfload /tmp/g15.conf || { bad "queue rules load"; finish; }

# 1) both classes are visible with statistics
Q=$(pf0 '/opt/pf/pfctl -s queue -v')
if echo "$Q" | grep -q 'rootq' && echo "$Q" | grep -q 'leafq'; then
	ok "pfctl -s queue lists both classes"
else
	bad "queue stats missing: $(echo "$Q" | head -2)"
fi

# 2) a queued flow completes
srv "pkill nc 2>/dev/null; head -c $SZ /dev/zero | tr '\\0' A > /tmp/pa.bin; sleep 1; (setsid nc -l -p 9095 < /tmp/pa.bin >/dev/null 2>&1 &); sleep 1" >/dev/null
T0=$(date +%s%N)
GOT=$(cli "nc -w60 10.0.2.20 9095 > /tmp/oa.bin; wc -c < /tmp/oa.bin" | tr -d '[:space:]')
T1=$(date +%s%N)
if [ "${GOT:-0}" -ge "$SZ" ]; then
	ok "queued flow completed ($GOT bytes in $(( (T1-T0)/1000000 )) ms)"
else
	bad "queued flow incomplete (got $GOT of $SZ)"
	finish
fi

# 3) the scheduler actually counted the class
sleep 1
Q2=$(pf0 '/opt/pf/pfctl -s queue -v')
# line after the leafq header looks like:  [ pkts:   37  bytes:  1932  dropped ...
LEAF_PKTS=$(printf '%s\n' "$Q2" | grep -A1 'leafq' | tail -1 |
	sed -n 's/^[[:space:]]*\[[[:space:]]*pkts:[[:space:]]*\([0-9][0-9]*\).*/\1/p')
if [ -n "$LEAF_PKTS" ] && [ "$LEAF_PKTS" -gt 0 ] 2>/dev/null; then
	ok "hfsc class counted traffic (leafq pkts=$LEAF_PKTS)"
else
	bad "hfsc class counters did not move: $(printf '%s\n' "$Q2" | grep -A1 leafq | tail -1)"
fi

# 4) the firewall is still healthy after queueing
pf0 'echo SSH_ALIVE' | grep -q SSH_ALIVE && ok "firewall responsive after queued transfer" \
	|| bad "firewall unresponsive after queueing"

pf0 '/opt/pf/pfctl -d' >/dev/null
srv 'pkill nc 2>/dev/null; true' >/dev/null
finish
