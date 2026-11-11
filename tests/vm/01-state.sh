#!/usr/bin/env bash
# M2 gate 1: stateful transit + state visibility
set -uo pipefail; cd "$(dirname "$0")"; . ./lib.sh
pfensure
pf0 '/opt/pf/pfctl -e; /opt/pf/pfctl -f /dev/null; echo READY' | grep -q READY || { bad load; finish; }
cat > /tmp/g1.conf <<'C'
pass in quick proto icmp keep state
pass out quick proto icmp keep state
C
pfload /tmp/g1.conf || { bad rules; finish; }
cli "ping -c 3 -W 2 $SRV_IP >/dev/null 2>&1" && ok "transit ping" || bad "transit ping"
N=$(pf0 '/opt/pf/pfctl -s states | grep -c "icmp"'); [ "${N:-0}" -ge 1 ] && ok "icmp state ($N)" || bad "no state"
pf0 '/opt/pf/pfctl -d' >/dev/null; finish
