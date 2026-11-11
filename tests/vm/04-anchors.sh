#!/usr/bin/env bash
# M2 gate 4: anchors + quick precedence
set -uo pipefail; cd "$(dirname "$0")"; . ./lib.sh
pfensure; pf0 '/opt/pf/pfctl -e; /opt/pf/pfctl -f /dev/null' >/dev/null
cat > /tmp/g4main.conf <<'C'
anchor "restricted"
block drop in quick proto icmp from any to any
pass in quick
pass out quick
C
pfload /tmp/g4main.conf || { bad mainload; finish; }
cat > /tmp/g4anc.conf <<'C'
pass in quick proto icmp from 10.0.1.10 to any
C
r=$(pfput /tmp/g4anc.conf); pf0 "/opt/pf/pfctl -a restricted -f $r" || { bad anchorload; finish; }
cli "ping -c1 -W2 $SRV_IP >/dev/null 2>&1" && ok "anchor pass evaluated before block" || bad "anchor pass not honored"
# anchor rules are evaluated BEFORE the main-ruleset block line (top placement)
pf0 '/opt/pf/pfctl -s Anchors' | grep -q restricted && ok "anchors listed" || bad "anchor list"
pf0 '/opt/pf/pfctl -d; /opt/pf/pfctl -f /dev/null' >/dev/null; finish
