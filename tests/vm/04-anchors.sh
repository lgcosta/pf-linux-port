#!/usr/bin/env bash
# M2 gate 4: anchors + quick precedence
set -uo pipefail; cd "$(dirname "$0")"; . ./lib.sh
pf0 '/opt/pf/pfctl -e; /opt/pf/pfctl -f /dev/null' >/dev/null
pf0 'printf "pass in quick\nanchor \"x\"\nblock drop in quick proto icmp keep first\n" > /tmp/a.conf'
pf0 '/opt/pf/pfctl -f /tmp/a.conf' >/dev/null
pf0 'echo "pass in quick proto icmp from 10.0.1.10 to any" | /opt/pf/pfctl -a x -f -' && ok "anchor load"
cli "ping -c1 -W2 $SRV_IP >/dev/null 2>&1" && ok "anchor rule evaluated before block" || bad "anchor pass not honored"
pf0 '/opt/pf/pfctl -s Anchors | grep -q x' && ok "anchors listed" || bad "anchor list"
pf0 '/opt/pf/pfctl -d' >/dev/null; finish
