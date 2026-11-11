#!/usr/bin/env bash
# M2 gate 2: block drop + block return-rst verified at packet level
set -uo pipefail; cd "$(dirname "$0")"; . ./lib.sh
pf0 '/opt/pf/pfctl -e; /opt/pf/pfctl -f /dev/null' >/dev/null
pf0 'echo "block drop in quick proto icmp from any to any
pass out quick" | /opt/pf/pfctl -f -'
cli "ping -c 2 -W 1 $SRV_IP >/dev/null 2>&1" && bad "icmp still passes" || ok "icmp blocked"
# return-rst on tcp: cli nc to closed-filtered port expects RST back
pf0 'echo "block return-rst in quick proto tcp from any to any port 8080
pass out quick" | /opt/pf/pfctl -f -'
srv 'timeout 3 nc -l -p 8080 >/dev/null 2>&1 &' ; sleep 0
cli 'timeout 3 nc -z 10.0.2.20 8080; echo NC=$?' | grep -q 'NC=1' && ok "tcp refused (rst)" || bad "tcp not refused"
pf0 '/opt/pf/pfctl -d' >/dev/null; finish
