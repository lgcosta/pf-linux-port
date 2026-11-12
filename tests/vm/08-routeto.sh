#!/usr/bin/env bash
# M4 gate: route-to / dup-to — forced forwarding path (pf_route).
#
# Lab: cli (10.0.1.10) pings srv (10.0.2.20).  Without the rule pf0 forwards it
# straight out eth2.  With `route-to { 10.0.3.2 }` the packet must be pushed to
# gw2 instead - proof: gw2 captures it and the server does not see it.
# Then `dup-to { 10.0.3.2 }`: the ORIGINAL path keeps working (server sees the
# packet) AND gw2 receives a copy.
set -uo pipefail; cd "$(dirname "$0")"; . ./lib.sh

pf0 'insmod /opt/pf/pf.ko 2>/dev/null || insmod /tmp/pf.ko; true'
pf0 '/opt/pf/pfctl -e; /opt/pf/pfctl -f /dev/null' >/dev/null

GW2_DEV=$(gw2 "ip route get $CLI_IP | grep -o 'dev [a-z0-9]*' | cut -d' ' -f2")
[ -n "$GW2_DEV" ] || GW2_DEV=eth1

capture_gw2() { gw2 "(tcpdump -ni $GW2_DEV -c 4 -e icmp > /tmp/gw2.td 2>&1 &)" 2>/dev/null; }
capture_srv() { srv "(tcpdump -ni eth1 -c 4 -e icmp > /tmp/srv.td 2>&1 &)" 2>/dev/null; }
sleep 1

# --- route-to: the packet is diverted to gw2 and never reaches the server ---
cat > /tmp/g08a.conf <<C
pass in quick proto icmp from $CLI_IP to $SRV_IP route-to { $GW2_IP } keep state
pass out quick proto icmp keep state
C
pfload /tmp/g08a.conf || { bad "route-to rule load"; finish; }
pf0 '/opt/pf/pfctl -sr' | grep -q 'route-to' && ok "route-to rule listed" || bad "route-to not listed"
capture_gw2; capture_srv
cli "ping -c2 -W2 $SRV_IP >/dev/null 2>&1"
sleep 2
gw2 'cat /tmp/gw2.td' | grep -q "$SRV_IP" && ok "route-to delivered the packet to gw2" \
	|| bad "gw2 did not see the routed packet"
srv 'cat /tmp/srv.td' | grep -q "$CLI_IP" && bad "server saw the packet (route not forced)" \
	|| ok "normal egress bypassed"

# --- dup-to: original path still works and gw2 gets a copy ---
cat > /tmp/g08b.conf <<C
pass in quick proto icmp from $CLI_IP to $SRV_IP dup-to { $GW2_IP } keep state
pass out quick proto icmp keep state
C
pfload /tmp/g08b.conf || { bad "dup-to rule load"; finish; }
pf0 '/opt/pf/pfctl -sr' | grep -q 'dup-to' && ok "dup-to rule listed" || bad "dup-to not listed"
capture_gw2; capture_srv
cli "ping -c2 -W2 $SRV_IP >/dev/null 2>&1" && ok "dup-to did not break the flow" \
	|| bad "dup-to broke the transit ping"
sleep 2
srv 'cat /tmp/srv.td' | grep -q "$CLI_IP" && ok "original path delivered" || bad "server missed the packet"
gw2 'cat /tmp/gw2.td' | grep -q "$SRV_IP" && ok "duplicate reached gw2" || bad "no duplicate at gw2"

pf0 '/opt/pf/pfctl -d' >/dev/null
finish
