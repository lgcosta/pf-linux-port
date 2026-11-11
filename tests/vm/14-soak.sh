#!/usr/bin/env bash
# M6 gate: soak + unload/leak discipline.
#
# 1) load pf with a stateful ruleset, drive mixed traffic for ~20s
# 2) assert no BUG/WARNING/Oops in dmesg
# 3) disable + rmmod: the module must unload cleanly (no refcount leak)
# 4) insmod/rmmod cycles: slab objects for the pf caches must return to the
#    pre-load baseline (no leak) and a reload must work
set -uo pipefail; cd "$(dirname "$0")"; . ./lib.sh

HELPER=$(pfput ./soak-helper.sh) || { bad "helper upload"; finish; }
pf0 "chmod +x $HELPER" >/dev/null

pf0 'insmod /opt/pf/pf.ko 2>/dev/null || insmod /tmp/pf.ko; true'
pf0 '/opt/pf/pfctl -e; /opt/pf/pfctl -f /dev/null' >/dev/null
BEFORE=$(pf0 "$HELPER slab")

cat > /tmp/g14.conf <<C
table <peers> { $CLI_IP }
pass in quick on ingress proto icmp from <peers> keep state
pass out quick on egress proto icmp keep state
pass out quick proto tcp from any to any port 22 keep state
C
pfload /tmp/g14.conf || { bad "soak ruleset"; finish; }

# mixed traffic: ICMP + TCP handshakes for ~20s
cli "ping -q -c 40 -i 0.5 $SRV_IP >/dev/null 2>&1" 2>/dev/null
cli "for i in 1 2 3 4 5; do nc -w1 -z $SRV_IP 22; done" 2>/dev/null

ST=$(pf0 "$HELPER states")
[ "${ST:-0}" -ge 1 ] && ok "states under load ($ST)" || bad "no states under load"

ANOM=$(pf0 "$HELPER dmesg")
if [ -z "$ANOM" ]; then
	ok "no kernel BUG/WARN/Oops"
else
	bad "dmesg anomalies:"
	printf '%s\n' "$ANOM" | sed 's/^/    /'
fi

pf0 '/opt/pf/pfctl -d; /opt/pf/pfctl -f /dev/null' >/dev/null
pf0 'rmmod pf && echo UNLOAD_OK' | grep -q UNLOAD_OK && ok "clean unload (refcounts released)" || bad "rmmod failed"
sleep 1
pf0 'insmod /opt/pf/pf.ko 2>/dev/null || insmod /tmp/pf.ko; true; (/opt/pf/pfctl -s info >/dev/null && echo RELOAD_OK)' \
	| grep -q RELOAD_OK && ok "reload after unload" || bad "reload failed"
pf0 'rmmod pf && echo UNLOAD_OK2' | grep -q UNLOAD_OK2 && ok "second unload" || bad "second rmmod"
sleep 1
AFTER=$(pf0 "$HELPER slab")
LEAK=$(join <(echo "$BEFORE") <(echo "$AFTER") 2>/dev/null | awk '$3-$2 > 0 {print $1, $3-$2}')
if [ -n "$LEAK" ]; then bad "slab growth after cycles: $LEAK"; else ok "no slab growth after unload cycles"; fi
finish
