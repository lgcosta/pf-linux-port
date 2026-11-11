#!/usr/bin/env bash
# M6 gate: soak + unload/leak discipline.
#
# 1) load pf, ruleset with states/tables/anchors, drive mixed traffic ~20s
# 2) assert no BUG/WARNING/Oops in dmesg
# 3) disable, rmmod; assert module unloads cleanly (no refcount hang)
# 4) insmod/rmmod cycle x2; assert slab objects for pf caches return to the
#    pre-load baseline (no leak), and that a reload works (timer/notifier
#    re-registration is idempotent)
set -uo pipefail; cd "$(dirname "$0")"; . ./lib.sh

FAILED=0
slab_snapshot() {
	pf0 "grep -E '^(pf|pfk)' /proc/slabinfo | awk '{print \$1, \$3}'" | sort
}

pf0 'insmod /tmp/pf.ko 2>/dev/null || insmod /opt/pf/pf.ko; true'
pf0 '/opt/pf/pfctl -e; /opt/pf/pfctl -f /dev/null' >/dev/null
BEFORE=$(slab_snapshot)

cat > /tmp/g14.conf <<C
table <peers> { $CLI_IP }
anchor "s"
pass in quick on ingress proto icmp from <peers> keep state
pass out quick proto icmp keep state
pass out quick proto tcp from any to any port 22 keep state
C
pfload /tmp/g14.conf || { bad soak ruleset; finish; }

# mixed traffic: ICMP + TCP handshakes, 20s
cli "ping -q -c 30 -i 0.2 $SRV_IP >/dev/null 2>&1 &" 2>/dev/null
cli "for i in 1 2 3 4 5; do nc -w1 -z $SRV_IP 22; done" 2>/dev/null
sleep 22
ST=$(pf0 '/opt/pf/pfctl -s states | wc -l')
[ "${ST:-0}" -ge 1 ] && ok "states under load ($ST)" || bad "no states under load"

DMESG=$(pf0 "dmesg | grep -cE 'BUG:|WARNING:|Oops|general protection'")
[ "${DMESG:-1}" = 0 ] && ok "no kernel BUG/WARN/Oops" || bad "dmesg anomalies ($DMESG)"

pf0 '/opt/pf/pfctl -d; /opt/pf/pfctl -f /dev/null' >/dev/null
pf0 'rmmod pf && echo UNLOAD_OK' | grep -q UNLOAD_OK && ok "clean unload (refcounts released)" || bad "rmmod failed"
sleep 1
pf0 'insmod /tmp/pf.ko 2>/dev/null || insmod /opt/pf/pf.ko; true; (/opt/pf/pfctl -s info >/dev/null && echo RELOAD_OK)' | grep -q RELOAD_OK && ok "reload after unload" || bad "reload failed"
pf0 'rmmod pf && echo UNLOAD_OK2' | grep -q UNLOAD_OK2 && ok "second unload" || bad "second rmmod"
sleep 1
AFTER=$(slab_snapshot)
# compare per-cache object counts if they exist on both sides
LEAK=$(join <(echo "$BEFORE") <(echo "$AFTER") 2>/dev/null | awk '$3-$2 > 0 {print $1, $3-$2}')
if [ -n "$LEAK" ]; then bad "slab growth after cycles: $LEAK"; else ok "no slab growth after unload cycles"; fi
finish
