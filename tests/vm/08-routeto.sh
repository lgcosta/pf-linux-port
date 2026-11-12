#!/usr/bin/env bash
# M4 gate: route-to / dup-to — forced forwarding path (pf_route).
#
# The lab's host bridge filters *forwarded* frames that cross two libvirt
# networks with a foreign source address, so the proof is taken on the
# firewall itself: which of pf0's egress interfaces the packet leaves from.
#
#   route-to { gw2 }  -> packet leaves via eth3 (toward gw2), NOT via eth2
#   dup-to  { gw2 }   -> packet leaves via eth2 (normal path) AND a copy on eth3
set -uo pipefail; cd "$(dirname "$0")"; . ./lib.sh

pf0 'insmod /opt/pf/pf.ko 2>/dev/null || insmod /tmp/pf.ko; true'
pf0 '/opt/pf/pfctl -e; /opt/pf/pfctl -f /dev/null' >/dev/null

# resolve pf0's interfaces by address (names are stable, but be safe)
IF_SRV=$(pf0 "ip route get $SRV_IP | grep -o 'dev [a-z0-9]*' | cut -d' ' -f2")
IF_GW=$(pf0 "ip route get $GW2_IP | grep -o 'dev [a-z0-9]*' | cut -d' ' -f2")
[ -n "$IF_SRV" ] && [ -n "$IF_GW" ] || { bad "interface resolution"; finish; }
[ "$IF_SRV" != "$IF_GW" ] || { bad "transit and gateway share an interface"; finish; }
ok "normal egress=$IF_SRV  forced egress=$IF_GW"


# count matching lines on pf0, robust against a missing/empty capture file
# (grep exits 1 on zero matches, and the ssh wrapper hides stderr)
cnt() {
	local f=$1 pat=$2 n
	n=$(pf0 "grep -c -- '$pat' $f 2>/dev/null; true" | tr -dc '0-9')
	[ -n "$n" ] || n=0
	printf '%s' "$n"
}

# capture both egresses at once
start_captures() {
	pf0 "pkill tcpdump; (tcpdump -ni $IF_SRV -c 6 -e icmp > /tmp/srv.td 2>&1 &); (tcpdump -ni $IF_GW -c 6 -e icmp > /tmp/gw.td 2>&1 &); sleep 1" >/dev/null
}

cat > /tmp/g08a.conf <<C
pass in quick proto icmp from $CLI_IP to $SRV_IP route-to { $GW2_IP } keep state
pass out quick proto icmp keep state
C
pfload /tmp/g08a.conf || { bad "route-to rule load"; finish; }
pf0 '/opt/pf/pfctl -sr' | grep -q 'route-to' && ok "route-to rule listed" || bad "route-to not listed"
start_captures
cli "ping -c2 -W2 $SRV_IP >/dev/null 2>&1"
sleep 2
# count only echo *requests* from the client (a reply also contains its address)
N=$(cnt /tmp/gw.td "$CLI_IP > $SRV_IP"); [ "$N" -gt 0 ] && ok "route-to: packet left via $IF_GW ($N)" \
	|| bad "route-to: nothing on $IF_GW"
N=$(cnt /tmp/srv.td "$CLI_IP > $SRV_IP"); [ "$N" -eq 0 ] && ok "route-to: normal egress $IF_SRV not used" \
	|| bad "route-to: $N requests also left via $IF_SRV"

cat > /tmp/g08b.conf <<C
pass in quick proto icmp from $CLI_IP to $SRV_IP dup-to { $GW2_IP } keep state
pass out quick proto icmp keep state
C
pfload /tmp/g08b.conf || { bad "dup-to rule load"; finish; }
pf0 '/opt/pf/pfctl -sr' | grep -q 'dup-to' && ok "dup-to rule listed" || bad "dup-to not listed"
start_captures
cli "ping -c2 -W2 $SRV_IP >/dev/null 2>&1"
sleep 2
N=$(cnt /tmp/srv.td "$CLI_IP > $SRV_IP"); [ "$N" -gt 0 ] && ok "dup-to: original path kept ($N on $IF_SRV)" \
	|| bad "dup-to: original path lost"
N=$(cnt /tmp/gw.td "$CLI_IP > $SRV_IP"); [ "$N" -gt 0 ] && ok "dup-to: duplicate sent to $IF_GW ($N)" \
	|| bad "dup-to: no duplicate"

pf0 'pkill tcpdump; /opt/pf/pfctl -d' >/dev/null
finish
