#!/usr/bin/env bash
# M2 gate 3: pfr tables via pfctl -t
set -uo pipefail; cd "$(dirname "$0")"; . ./lib.sh
pfensure; pf0 '/opt/pf/pfctl -e' >/dev/null
cat > /tmp/g3.conf <<'C'
table <blk> { 10.0.1.10 }
block in quick proto icmp from <blk> to any
pass in quick
pass out quick
C
pfload /tmp/g3.conf || { bad load; finish; }
pf0 '/opt/pf/pfctl -t blk -T show' | grep -q 10.0.1.10 && ok "table loaded" || bad "table empty"
cli "ping -c1 -W1 $SRV_IP >/dev/null 2>&1" && bad "blocked src passed" || ok "table block works"
pf0 '/opt/pf/pfctl -t blk -T delete 10.0.1.10' >/dev/null
cli "ping -c1 -W2 $SRV_IP >/dev/null 2>&1" && ok "unblocked passes" || bad "delete failed"
pf0 '/opt/pf/pfctl -d' >/dev/null; finish
