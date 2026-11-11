#!/usr/bin/env bash
# M2 gate 4: anchors + quick precedence
set -uo pipefail; cd "$(dirname "$0")"; . ./lib.sh
pf0 'insmod /tmp/pf.ko 2>/dev/null || insmod /opt/pf/pf.ko; true'; pf0 '/opt/pf/pfctl -e; /opt/pf/pfctl -f /dev/null' >/dev/null
cat > /tmp/g4main.conf <<'C'
anchor "restricted"
block drop in quick proto icmp from any to any
pass in quick
pass out quick
C
pfload /tmp/g4main.conf || { bad mainload; finish; }
cat > /tmp/g4anc.conf <<'C'
pass in quick proto icmp from 10.0.1.10 to any keep state
C
r=$(pfput /tmp/g4anc.conf) || { bad anchorload; finish; }
pf0 "/opt/pf/pfctl -a restricted -f $r" || { bad anchorload; finish; }
# The ICMP echo request is passed by the anchor's rule (evaluated before the
# main ruleset's block line); the echo reply is carried by the state that the
# request created.  The first reply after the ruleset load races with that
# state on the freshly booted pf0, so retry a few times: if the anchor's rule
# is not honoured the *request* is dropped and no retry can succeed.
pass_ok=0
for _ in 1 2 3 4 5; do
	if cli "ping -c1 -W2 $SRV_IP >/dev/null 2>&1"; then pass_ok=1; break; fi
	sleep 1
done
[ "$pass_ok" = 1 ] && ok "anchor pass evaluated before block" || bad "anchor pass not honored"
# anchor rules are evaluated BEFORE the main-ruleset block line (top placement)
pf0 '/opt/pf/pfctl -s Anchors' | grep -q restricted && ok "anchors listed" || bad "anchor list"
pf0 '/opt/pf/pfctl -d; /opt/pf/pfctl -f /dev/null' >/dev/null; finish
