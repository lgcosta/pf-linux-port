#!/usr/bin/env bash
# M8 gate: dynamic address pools (nat-to (iface)) follow the interface's
# current addresses.
#
# The pool behind "nat-to (eth3)" is resolved by the kernel from the
# interface's addresses.  Before the fix, the table behind it was only
# rebuilt when an interface was attached or a group changed - never when an
# address was added or removed.  A ruleset applied before the WAN address
# existed therefore kept an *empty* pool (no translation at all) and one
# applied before a DHCP change kept the *old* address, until the next
# ruleset reload - the boot race reported in issue #2.
#
# What this gate proves:
#   1) with the interface's address gone, a freshly loaded ruleset does not
#      translate (the pool is empty - correct, there is no target);
#   2) when the address lands, the pool is re-published *without a ruleset
#      reload* and the traffic is translated;
#   3) "nat-to (egress)" resolves to the default-route interface's address
#      (before the fix the emulated egress group resolved to nothing).
set -uo pipefail; cd "$(dirname "$0")"; . ./lib.sh

IFACE=${DYNADDR_IFACE:-eth3}		# wan2: not in the transit path
ADDR=${DYNADDR_ADDR:-10.0.3.1/24}
ADDR_IP=${ADDR%%/*}

# the gate is only meaningful if the lab still has the interface and its
# address (the lab topology is fixed, but say so loudly if it changed)
if ! pf0 "ip -br addr show $IFACE | grep -q '$ADDR_IP'"; then
	bad "$IFACE has no $ADDR_IP - the lab topology changed"
	finish
fi

pf0 'insmod /opt/pf/pf.ko 2>/dev/null || insmod /tmp/pf.ko; true'
pf0 '/opt/pf/pfctl -e; /opt/pf/pfctl -f /dev/null' >/dev/null

cat > /tmp/g17.conf <<C
pass out quick on eth2 from $CLI_IP to any nat-to ($IFACE)
pass out quick keep state
C
pfload /tmp/g17.conf || { bad "ruleset"; finish; }

# --- 1) the interface has no address: the pool must be empty ---------------
pf0 "ip addr del $ADDR dev $IFACE 2>/dev/null; /opt/pf/pfctl -F s" >/dev/null
pfload /tmp/g17.conf >/dev/null || { bad "ruleset reload"; finish; }
cli "ping -c 2 -W 2 $SRV_IP >/dev/null 2>&1"
sleep 1
if pf0 '/opt/pf/pfctl -s states' | grep -q "($CLI_IP"; then
	bad "traffic was translated although $IFACE has no address"
else
	ok "no translation while $IFACE has no address (pool empty)"
fi

# --- 2) the address lands: the pool must be re-published, no reload ---------
pf0 "ip addr add $ADDR dev $IFACE" >/dev/null
HEALED=0
for i in $(seq 1 10); do		# the compat layer polls once a second
	sleep 1
	pf0 '/opt/pf/pfctl -F s' >/dev/null
	cli "ping -c 1 -W 2 $SRV_IP >/dev/null 2>&1"
	if pf0 '/opt/pf/pfctl -s states' | grep -q "($CLI_IP"; then
		HEALED=$i
		break
	fi
done
if [ "$HEALED" -gt 0 ]; then
	ok "pool re-published ${HEALED}s after the address landed (no ruleset reload)"
else
	bad "pool still not re-published 10s after the address landed"
fi
# the translation must carry the interface's address
if pf0 '/opt/pf/pfctl -s states' | grep -q "$ADDR_IP"; then
	ok "translated to the interface address $ADDR_IP"
else
	bad "translation does not use $ADDR_IP"
fi

# --- 3) nat-to (egress) resolves to the default-route interface -------------
EGRESS_IP=$(pf0 "ip route get 8.8.8.8 | grep -o 'src [0-9.]*' | cut -d' ' -f2" | tr -d '[:space:]')
cat > /tmp/g17e.conf <<C
pass out quick on eth2 from $CLI_IP to any nat-to (egress)
pass out quick keep state
C
pfload /tmp/g17e.conf >/dev/null || { bad "egress ruleset"; finish; }
pf0 '/opt/pf/pfctl -F s' >/dev/null
cli "ping -c 2 -W 2 $SRV_IP >/dev/null 2>&1"
sleep 1
if [ -n "$EGRESS_IP" ] && pf0 '/opt/pf/pfctl -s states' | grep -q "$EGRESS_IP"; then
	ok "nat-to (egress) resolves to the default-route address $EGRESS_IP"
else
	bad "nat-to (egress) did not resolve (expected $EGRESS_IP)"
fi

# --- cleanup ---------------------------------------------------------------
pf0 "ip addr show dev $IFACE | grep -q '$ADDR_IP' || ip addr add $ADDR dev $IFACE" >/dev/null
pf0 '/opt/pf/pfctl -d; /opt/pf/pfctl -f /dev/null' >/dev/null
finish
