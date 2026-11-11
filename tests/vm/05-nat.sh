#!/usr/bin/env bash
# M3 gate: nat-to — source NAT of transit traffic to the egress address.
#
# Scenario: cli (10.0.1.10) -> srv (10.0.2.20) through pf0, rule:
#   nat on egress proto icmp from 10.0.1.0/24 to any -> (egress)
# Expected: srv sees src 192.168.122.8 (pf0 egress IP) instead of 10.0.1.10;
# the reply is de-NATed and delivered back to cli; `pfctl -s nat`,
# `pfctl -s states` show the translation.
set -uo pipefail; cd "$(dirname "$0")"; . ./lib.sh

FAILED=0
pf0 "insmod /tmp/pf.ko 2>/dev/null || insmod /opt/pf/pf.ko; true"
pf0 '/opt/pf/pfctl -e; /opt/pf/pfctl -f /dev/null' >/dev/null

cat > /tmp/g05.conf <<C
nat on egress proto icmp from 10.0.1.0/24 to any -> (egress)
pass in quick on ingress proto icmp keep state
pass out quick proto icmp keep state
C
pfload /tmp/g05.conf || { bad "nat rule load"; finish; }

pf0 '/opt/pf/pfctl -sn' | grep -q 'nat on egress' && ok "nat rule listed" || bad "nat not listed"

# capture on pf0's egress while cli pings srv
pf0 'tcpdump -ni egress -c 4 -e icmp > /tmp/nat.td 2>&1 &'
sleep 1
cli "ping -c 2 -W 2 $SRV_IP >/dev/null 2>&1" && ok "transit ping through NAT" || bad "transit ping"
sleep 2
TD=$(pf0 'cat /tmp/nat.td')
echo "$TD" | grep -q "$CLI_IP" && bad "cli source address leaked to egress" || ok "source rewritten (no $CLI_IP on egress)"
echo "$TD" | grep -q '192.168.122.8' && ok "nat applied to $(pf0 'ip -4 addr show egress | grep -o "192[0-9.]*"' | head -1)" || bad "egress src ip not seen"

pf0 '/opt/pf/pfctl -s states' | grep -q "$SRV_IP" && ok "state present" || bad "no state"
pf0 '/opt/pf/pfctl -d' >/dev/null
finish
