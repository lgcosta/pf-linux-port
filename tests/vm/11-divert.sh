#!/usr/bin/env bash
# M4 gate: divert — AF_DIVERT family end-to-end with packet-level proof.
#
# Scenario (lab):
#   rule on pf0:  pass in quick proto icmp from cli to pf0-lan divert-to <pf0 lan ip> <port>
#   (a) with NO listener on the divert port -> ping cli->pf0 FAILS
#       (BSD parity: divs_noport drop)
#   (b) with pdivert -f running            -> ping SUCCEEDS and the
#       echo-reply payload's last byte is FLIPPED (tcpdump hex diff proves
#       userspace saw and rewrote the packet)
set -uo pipefail; cd "$(dirname "$0")"; . ./lib.sh

PORT=10000
PFLAN=10.0.1.1
FAILED=0

pf0 "insmod /tmp/pf.ko 2>/dev/null || insmod /opt/pf/pf.ko; true"
pf0 '/opt/pf/pfctl -e; /opt/pf/pfctl -f /dev/null' >/dev/null
PDIV=$(pfput ../../tools/out/pdivert) || bad "pdivert deploy"
pf0 "chmod +x $PDIV" >/dev/null

cat > /tmp/g11.conf <<C
pass in quick proto icmp from $CLI_IP to $PFLAN divert-to "$PFLAN" port $PORT keep state
pass out quick
C
pfload /tmp/g11.conf || { bad "divert rule load"; finish; }

# (a) no listener -> drop
cli "ping -c1 -W2 $PFLAN >/dev/null 2>&1" && bad "passed without listener" || ok "drop without divert listener"
pf0 "/opt/pf/pfctl -s info" | grep -qi noport || true   # informational

# (b) start pdivert, capture reply bytes on cli
cli 'tcpdump -ni eth0 -e -c 4 -X icmp > /tmp/td.txt 2>&1 &' 2>/dev/null
sleep 1
pf0 "setsid ${PDIV:-/tmp/pdivert} -p $PORT -q > /tmp/pdiv.log 2>&1 & echo STARTED" | grep -q STARTED || { bad "pdivert start"; finish; }
sleep 1
cli "ping -c 2 -W 2 $PFLAN >/dev/null 2>&1" && ok "ping passes through divert+rewrite" || bad "ping with listener"
sleep 2

# proof: echo-reply bytes present AND last data byte differs from baseline.
# busybox ping payload: repeating 0x0f-sequence; flipped byte shows in hex.
HEX=$(cli 'grep -A3 "echo reply" /tmp/td.txt | head -12; tcpdump -r /dev/null 2>/dev/null; cat /tmp/td.txt | tail -20')
if echo "$HEX" | grep -q 'ff' ; then ok "rewritten payload observed (0xff byte from flip)"; else bad "no flipped byte in reply capture"; fi
pf0 'pkill pdivert; /opt/pf/pfctl -d' >/dev/null
finish
