#!/usr/bin/env bash
# M3 gate: nat-to — source NAT of transit traffic to the egress address.
#
# OpenBSD 8.0 unified the old "nat on ..." rule into the rule syntax:
#   pass out on egress from <net> to any nat-to (egress)
#
# Expected: the reply path is unaffected, srv sees the packet with
# src = pf0's egress address (not the client's), and pfctl shows the rule.
set -uo pipefail; cd "$(dirname "$0")"; . ./lib.sh

FAILED=0
# pf calls the interface "egress"; the OS names it ethX - resolve both
EGRESS_DEV=$(pf0 "ip route get $SRV_IP | grep -o 'dev [a-z0-9]*' | cut -d' ' -f2")
EGRESS_IP=$(pf0 "ip -4 addr show $EGRESS_DEV | grep -o 'inet [0-9.]*' | head -1 | cut -d' ' -f2")
[ -n "$EGRESS_IP" ] || EGRESS_IP=192.168.122.8

pf0 'insmod /opt/pf/pf.ko 2>/dev/null || insmod /tmp/pf.ko; true'
pf0 '/opt/pf/pfctl -e; /opt/pf/pfctl -f /dev/null' >/dev/null

cat > /tmp/g05.conf <<C
pass out quick on egress from 10.0.1.0/24 to any nat-to (egress)
pass in quick on ingress proto icmp keep state
pass out quick on egress proto icmp keep state
C
pfload /tmp/g05.conf || { bad "nat rule load"; finish; }
pf0 '/opt/pf/pfctl -sr' | grep -q 'nat-to' && ok "nat-to rule listed" || bad "nat-to not listed"

pf0 "tcpdump -ni $EGRESS_DEV -c 4 -e icmp > /tmp/nat.td 2>&1 &"
sleep 1
cli "ping -c2 -W2 $SRV_IP >/dev/null 2>&1" && ok "transit ping through NAT" || bad "transit ping"
sleep 2

TD=$(pf0 'cat /tmp/nat.td')
if echo "$TD" | grep -q "$CLI_IP"; then
	bad "client address $CLI_IP still visible on egress"
else
	ok "source rewritten on egress (no $CLI_IP)"
fi
echo "$TD" | grep -q "$EGRESS_IP" && ok "egress source is $EGRESS_IP" || bad "egress source not seen"

pf0 '/opt/pf/pfctl -ss' | grep -q "$SRV_IP" && ok "state present" || bad "no state"
pf0 '/opt/pf/pfctl -d' >/dev/null
finish
