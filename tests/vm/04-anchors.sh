#!/usr/bin/env bash
# M2 gate 4: anchors + evaluation order (OpenBSD semantics)
#
# OpenBSD evaluates the IN ruleset in ip_input() for every arriving packet and
# the OUT ruleset in ip_forward() for forwarded packets.  A `block in ... icmp`
# rule therefore also sees the *reply* of a transit flow - a state created by
# an anchor rule does NOT rescue the reply at input time.  This gate keeps the
# ruleset OpenBSD-correct and isolates the anchor semantics:
#
#   anchor "restricted"          -> child: pass in quick ... from <client> keep state
#   block drop in quick proto icmp from any to <server>   (would drop the request)
#   pass in quick / pass out quick
#
# The request must be passed by the ANCHOR child rule (the later block rule
# would drop it), and a state must exist.  After flushing the anchor the very
# same request must be dropped - proving the anchor is what allowed it.
set -uo pipefail; cd "$(dirname "$0")"; . ./lib.sh

pf0 'insmod /opt/pf/pf.ko 2>/dev/null || insmod /tmp/pf.ko; true'
pf0 '/opt/pf/pfctl -e; /opt/pf/pfctl -f /dev/null' >/dev/null

cat > /tmp/g4main.conf <<C
anchor "restricted"
block drop in quick proto icmp from any to $SRV_IP
pass in quick
pass out quick
C
cat > /tmp/g4anc.conf <<C
pass in quick proto icmp from $CLI_IP to any keep state
C

pfload /tmp/g4main.conf || { bad "main ruleset load"; finish; }
ANC=$(pfput /tmp/g4anc.conf) || { bad "anchor conf upload"; finish; }
pf0 "/opt/pf/pfctl -a restricted -f $ANC" || { bad "anchor ruleset load"; finish; }

pf0 '/opt/pf/pfctl -sr' | grep -q 'anchor "restricted"' && ok "anchor rule in main ruleset" || bad "anchor rule missing"
pf0 '/opt/pf/pfctl -a restricted -sr' | grep -q 'keep state' && ok "anchor rules listed" || bad "anchor rules missing"

# the request must pass through the anchor (the block rule would drop it)
cli "ping -c2 -W2 $SRV_IP >/dev/null 2>&1" && ok "anchor pass honoured (request allowed)" \
	|| bad "anchor pass not honored"
pf0 '/opt/pf/pfctl -ss' | grep -q "$SRV_IP" && ok "state created by anchor rule" || bad "no state from anchor"

# without the anchor rule the request hits the block rule -> dropped
pf0 "/opt/pf/pfctl -a restricted -F all" >/dev/null 2>&1
cli "ping -c1 -W2 $SRV_IP >/dev/null 2>&1" && bad "request passed without anchor rule" \
	|| ok "request dropped without anchor rule (block applies)"

pf0 '/opt/pf/pfctl -d; /opt/pf/pfctl -f /dev/null' >/dev/null
finish
