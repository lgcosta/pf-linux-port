#!/usr/bin/env bash
# M3 gate: scrub - normalization actually rewrites packets.
#
# 8.0 syntax: scrub is a rule option ("match in all scrub (...)"), not a
# standalone rule.  Proof is taken on pf0's own egress (the lab's host bridge
# filters forwarded frames between networks, so peer-side captures are not
# reliable):
#   min-ttl 100   -> a transit packet with ttl 64 leaves with ttl 100
#   max-mss 500   -> a TCP SYN with MSS 1460 leaves advertising MSS 500
set -uo pipefail; cd "$(dirname "$0")"; . ./lib.sh

pf0 'insmod /opt/pf/pf.ko 2>/dev/null || insmod /tmp/pf.ko; true'
pf0 '/opt/pf/pfctl -e; /opt/pf/pfctl -f /dev/null' >/dev/null

IF_SRV=$(pf0 "ip route get $SRV_IP | grep -o 'dev [a-z0-9]*' | cut -d' ' -f2")
[ -n "$IF_SRV" ] || IF_SRV=eth2

cat > /tmp/g09.conf <<C
set reassemble yes
match in all scrub (min-ttl 100 max-mss 500)
pass in quick proto icmp from $CLI_IP keep state
pass in quick proto tcp from $CLI_IP to $SRV_IP port 9092 keep state
pass out quick keep state
C
pfload /tmp/g09.conf || { bad "scrub rules load"; finish; }
pf0 '/opt/pf/pfctl -sr' | grep -q 'scrub' && ok "scrub rule listed" || bad "scrub not listed"

# --- min-ttl on a transit ICMP packet ---
# OpenBSD raises the TTL to min-ttl in pf, then ip_forward() decrements it for
# the hop, so the egress packet shows min-ttl-1 (100 -> 99).
pf0 "pkill tcpdump; (tcpdump -ni $IF_SRV -c 3 -vv icmp > /tmp/scr1.td 2>&1 &); sleep 1"
cli "ping -c2 -W2 $SRV_IP >/dev/null 2>&1"
sleep 2
N=$(pf0 "grep -c 'ttl 99' /tmp/scr1.td 2>/dev/null; true" | tr -dc '0-9')
if [ "${N:-0}" -gt 0 ]; then
	ok "min-ttl applied (ttl raised to 100, forwarded as 99)"
else
	DIAG=$(pf0 "grep -o 'ttl [0-9]*' /tmp/scr1.td 2>/dev/null | head -2" | tr '\n' ' ')
	bad "min-ttl not applied: $DIAG"
fi

# --- max-mss on a TCP SYN ---
pf0 "(tcpdump -ni $IF_SRV -c 3 -vv tcp port 9092 > /tmp/scr2.td 2>&1 &); sleep 1"
cli "nc -w1 -z $SRV_IP 9092 >/dev/null 2>&1"
sleep 2
if pf0 "grep -q 'mss 500' /tmp/scr2.td" 2>/dev/null; then
	ok "max-mss clamped to 500"
else
	DIAG=$(pf0 "grep -o 'mss [0-9]*' /tmp/scr2.td 2>/dev/null | head -2" | tr '\n' ' ')
	bad "mss not clamped: $DIAG"
fi

pf0 'pkill tcpdump; /opt/pf/pfctl -d' >/dev/null
finish
