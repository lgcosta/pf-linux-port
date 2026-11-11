#!/usr/bin/env bash
# M2 gate 3: pfr tables via pfctl -t
set -uo pipefail; cd "$(dirname "$0")"; . ./lib.sh
pf0 '/opt/pf/pfctl -e' >/dev/null
pf0 'printf "block in quick proto icmp from <blk>\npass out quick\n" | /opt/pf/pfctl -f -'
pf0 '/opt/pf/pfctl -t blk -T add 10.0.1.10' && ok "table add"
M=$(pf0 '/opt/pf/pfctl -t blk -T show | grep -c 10.0.1.10'); [ "$M" -ge 1 ] && ok "table shows entry" || bad "table empty"
cli "ping -c1 -W1 $SRV_IP >/dev/null 2>&1" && bad "blocked src passed" || ok "table block works"
pf0 "/opt/pf/pfctl -t blk -T kill 10.0.1.10" >/dev/null
cli "ping -c1 -W2 $SRV_IP >/dev/null 2>&1" && ok "unblocked passes" || bad "kill failed"
pf0 '/opt/pf/pfctl -d' >/dev/null; finish
