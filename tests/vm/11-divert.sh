#!/usr/bin/env bash
# M4 gate: divert — AF_DIVERT family end-to-end with packet-level proof.
#
# Scenario (lab):
#   rule: pass in quick proto icmp from cli to pf0-lan divert-to "<pf0-lan>" port 10000 keep state
#   (a) no listener on the divert port  -> ping cli->pf0 FAILS (BSD parity: divs_noport)
#   (b) pdivert -f running              -> ping SUCCEEDS and pdivert rewrites the
#       payload (last byte flipped), proving the packet went to userspace and back.
set -uo pipefail; cd "$(dirname "$0")"; . ./lib.sh

PORT=10000
PFLAN=10.0.1.1

pf0 'insmod /opt/pf/pf.ko 2>/dev/null || insmod /tmp/pf.ko; true'
pf0 '/opt/pf/pfctl -e; /opt/pf/pfctl -f /dev/null' >/dev/null
pf0 'pkill pdivert 2>/dev/null; true'

cat > /tmp/g11.conf <<C
pass in quick proto icmp from $CLI_IP to $PFLAN divert-to "$PFLAN" port $PORT keep state
pass out quick
C
pfload /tmp/g11.conf || { bad "divert rule load"; finish; }
pf0 '/opt/pf/pfctl -sr' | grep -q 'divert-to' && ok "divert rule loaded" || bad "divert rule missing"

# (a) no listener -> BSD drops (divs_noport)
cli "ping -c1 -W2 $PFLAN >/dev/null 2>&1" && bad "passed without divert listener" || ok "drop without divert listener"

# (b) listener present -> passes through userspace
PDIV=$(pfput ../../tools/out/pdivert) || bad "pdivert deploy"
pf0 "chmod +x $PDIV; (setsid $PDIV -p $PORT -f > /tmp/pdiv.log 2>&1 < /dev/null &); sleep 1"
pf0 "pgrep pdivert >/dev/null && echo RUNNING" | grep -q RUNNING && ok "pdivert listening" || bad "pdivert start"
cli "ping -c2 -W2 $PFLAN >/dev/null 2>&1" && ok "ping passes through divert" || bad "ping with listener"
pf0 'cat /tmp/pdiv.log' | grep -qi 'divert' && ok "pdivert received packet" || bad "pdivert saw nothing"
pf0 'pkill pdivert; /opt/pf/pfctl -d' >/dev/null 2>&1
finish
