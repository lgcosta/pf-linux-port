#!/usr/bin/env bash
# M3 gate: binat-to — bidirectional (static 1:1) NAT.
#
# Lab: the server (10.0.2.20) pings the client (10.0.1.10) through pf0.  The
# binat rule rewrites the source to 10.0.2.50; the client's reply goes back to
# 10.0.2.50, which pf0 owns in the mapping, and is translated to the server.
#
# Proves: outbound source rewrite + inbound destination rewrite with one rule,
# and that the reply survives the state/translation path.
set -uo pipefail; cd "$(dirname "$0")"; . ./lib.sh

BINAT=10.0.2.50

pf0 'insmod /opt/pf/pf.ko 2>/dev/null || insmod /tmp/pf.ko; true'
pf0 '/opt/pf/pfctl -e; /opt/pf/pfctl -f /dev/null' >/dev/null

cat > /tmp/g07.conf <<C
pass quick proto icmp from $SRV_IP to $CLI_IP binat-to $BINAT keep state
pass in quick proto icmp keep state
pass out quick proto icmp keep state
C
pfload /tmp/g07.conf || { bad "binat rule load"; finish; }
# pfctl expands one binat-to rule into the documented pair:
#   pass out ... nat-to <mapped> static-port   (outbound half)
#   pass in  ... rdr-to <real>                 (inbound half)
RULES=$(pf0 '/opt/pf/pfctl -sr')
echo "$RULES" | grep -q "nat-to $BINAT static-port" && ok "binat outbound half installed" || bad "outbound half missing"
echo "$RULES" | grep -q "rdr-to $SRV_IP" && ok "binat inbound half installed" || bad "inbound half missing"

# capture on the client side (resolve its transit interface, like pf0's)
CLI_DEV=$(cli "ip route get $SRV_IP | grep -o 'dev [a-z0-9]*' | cut -d' ' -f2")
[ -n "$CLI_DEV" ] || CLI_DEV=eth1
cli "(tcpdump -ni $CLI_DEV -c 4 -e icmp > /tmp/bi.td 2>&1 &)" 2>/dev/null
sleep 1
srv "ping -c2 -W2 $CLI_IP >/dev/null 2>&1" && ok "ping srv->cli through binat" || bad "binat round trip"
sleep 2

TD=$(cli 'cat /tmp/bi.td' 2>/dev/null)
if echo "$TD" | grep -q "$BINAT"; then
	ok "request arrived from the mapped address $BINAT"
else
	bad "mapped address not seen on the client: $(echo "$TD" | head -2)"
fi
echo "$TD" | grep -q "$SRV_IP" && bad "original address $SRV_IP leaked" || ok "original source not leaked"

pf0 '/opt/pf/pfctl -ss' | grep -q "$BINAT" && ok "state shows the mapping" || bad "no binat state"
pf0 '/opt/pf/pfctl -d' >/dev/null
finish
