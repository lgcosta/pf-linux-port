#!/usr/bin/env bash
# M4 gate: route-to pools - round-robin vs source-hash (pf_lb).
#
# Two gateways live on two different firewall links:
#   10.0.3.2 via $IF_GW   (gw2's 10.0.3 side)
#   10.0.2.2 via $IF_SRV  (gw2's 10.0.2 side)
# Each ping is a separate flow (own state), so the pool choice is made per
# flow: round-robin must spread the flows over both links, source-hash must
# keep them on one (same source address -> same bucket).
set -uo pipefail; cd "$(dirname "$0")"; . ./lib.sh

GW1=$GW2_IP			# 10.0.3.2
GW2=10.0.2.2

pf0 'insmod /opt/pf/pf.ko 2>/dev/null || insmod /tmp/pf.ko; true'
pf0 '/opt/pf/pfctl -e; /opt/pf/pfctl -f /dev/null' >/dev/null

IF_GW=$(pf0 "ip route get $GW1 | grep -o 'dev [a-z0-9]*' | cut -d' ' -f2")
IF_SRV=$(pf0 "ip route get $SRV_IP | grep -o 'dev [a-z0-9]*' | cut -d' ' -f2")
[ -n "$IF_GW" ] && [ -n "$IF_SRV" ] || { bad "interface resolution"; finish; }
[ "$IF_GW" != "$IF_SRV" ] || { bad "the two gateways share a link"; finish; }
ok "pool links: $IF_GW ($GW1) and $IF_SRV ($GW2)"

count_on() {	# count echo requests captured on an interface
	local n
	n=$(pf0 "grep -c '$CLI_IP > $SRV_IP' /tmp/pool_$1.td 2>/dev/null; true" | tr -dc '0-9')
	[ -n "$n" ] || n=0
	printf '%s' "$n"
}

run_flows() {	# $1 = ruleset file, four separate ICMP flows
	pf0 "pkill tcpdump; (tcpdump -ni $IF_GW -c 8 -e icmp > /tmp/pool_gw.td 2>&1 &); (tcpdump -ni $IF_SRV -c 8 -e icmp > /tmp/pool_srv.td 2>&1 &); sleep 1" >/dev/null
	cli "for i in 1 2 3 4; do ping -c1 -W2 $SRV_IP >/dev/null 2>&1; done"
	sleep 2
}

# --- round-robin: the flows must spread over both links ---
cat > /tmp/g10a.conf <<C
pass in quick proto icmp from $CLI_IP to $SRV_IP route-to { $GW1, $GW2 } round-robin keep state
pass out quick keep state
C
pfload /tmp/g10a.conf || { bad "rr rule load"; finish; }
run_flows
A=$(count_on gw); B=$(count_on srv)
if [ "$A" -gt 0 ] && [ "$B" -gt 0 ]; then
	ok "round-robin spread the flows ($A on $IF_GW, $B on $IF_SRV)"
else
	bad "round-robin did not spread ($A on $IF_GW, $B on $IF_SRV)"
fi

# --- source-hash: the same source sticks to one gateway ---
cat > /tmp/g10b.conf <<C
pass in quick proto icmp from $CLI_IP to $SRV_IP route-to { $GW1, $GW2 } source-hash keep state
pass out quick keep state
C
pfload /tmp/g10b.conf || { bad "source-hash rule load"; finish; }
run_flows
A=$(count_on gw); B=$(count_on srv)
if [ "$((A + B))" -ge 3 ] && { [ "$A" -eq 0 ] || [ "$B" -eq 0 ]; }; then
	ok "source-hash pinned the flows to one link ($A/$B)"
else
	bad "source-hash spread the flows ($A on $IF_GW, $B on $IF_SRV)"
fi

pf0 'pkill tcpdump; /opt/pf/pfctl -d' >/dev/null
finish
