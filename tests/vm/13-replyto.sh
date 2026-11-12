#!/usr/bin/env bash
# M4 gate: reply-to - force the next hop of the *reply* direction.
#
# reply-to is the asymmetric-routing tool: a flow whose request arrives on one
# link can have its answer pushed out through a different gateway.  Proof is
# taken on pf0's own interfaces (the lab's host bridge filters forwarded frames
# that cross networks, so the peer side is not observable):
#
#   request  cli -> srv   leaves via $IF_SRV (normal path, untouched)
#   reply    srv -> cli   leaves via $IF_GW  (forced by reply-to), not $IF_CLI
set -uo pipefail; cd "$(dirname "$0")"; . ./lib.sh

pf0 'insmod /opt/pf/pf.ko 2>/dev/null || insmod /tmp/pf.ko; true'
pf0 '/opt/pf/pfctl -e; /opt/pf/pfctl -f /dev/null' >/dev/null

IF_SRV=$(pf0 "ip route get $SRV_IP | grep -o 'dev [a-z0-9]*' | cut -d' ' -f2")
IF_GW=$(pf0 "ip route get $GW2_IP | grep -o 'dev [a-z0-9]*' | cut -d' ' -f2")
IF_CLI=$(pf0 "ip route get $CLI_IP | grep -o 'dev [a-z0-9]*' | cut -d' ' -f2")
[ -n "$IF_SRV" ] && [ -n "$IF_GW" ] && [ -n "$IF_CLI" ] || { bad "interface resolution"; finish; }
[ "$IF_GW" != "$IF_CLI" ] || { bad "gateway and client share a link"; finish; }
ok "reply would normally leave via $IF_CLI, forced via $IF_GW"

cat > /tmp/g13.conf <<C
pass in quick proto icmp from $CLI_IP to $SRV_IP reply-to { $GW2_IP } keep state
pass out quick keep state
C
pfload /tmp/g13.conf || { bad "reply-to rule load"; finish; }
pf0 '/opt/pf/pfctl -sr' | grep -q 'reply-to' && ok "reply-to rule listed" || bad "reply-to not listed"

pf0 "pkill tcpdump; (tcpdump -ni $IF_GW -c 8 -e icmp > /tmp/rt_gw.td 2>&1 &); (tcpdump -ni $IF_CLI -c 8 -e icmp > /tmp/rt_cli.td 2>&1 &); (tcpdump -ni $IF_SRV -c 8 -e icmp > /tmp/rt_srv.td 2>&1 &); sleep 1" >/dev/null
cli "ping -c2 -W2 $SRV_IP >/dev/null 2>&1"
sleep 2

cnt() {
	local n
	n=$(pf0 "grep -c -- '$2' /tmp/rt_$1.td 2>/dev/null; true" | tr -dc '0-9')
	[ -n "$n" ] || n=0
	printf '%s' "$n"
}

# the request path must be unchanged
N=$(cnt srv "$CLI_IP > $SRV_IP"); [ "$N" -gt 0 ] && ok "request left via $IF_SRV ($N)" \
	|| bad "request did not leave via $IF_SRV"

# the reply must be forced to the gateway link
N=$(cnt gw "$SRV_IP > $CLI_IP"); [ "$N" -gt 0 ] && ok "reply forced onto $IF_GW ($N)" \
	|| bad "reply not seen on $IF_GW"
N=$(cnt cli "$SRV_IP > $CLI_IP"); [ "$N" -eq 0 ] && ok "reply did not use $IF_CLI" \
	|| bad "reply still left via $IF_CLI ($N)"

pf0 'pkill tcpdump; /opt/pf/pfctl -d' >/dev/null
finish
