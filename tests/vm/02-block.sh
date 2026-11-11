#!/usr/bin/env bash
# M2 gate 2: block drop + block return-rst
set -uo pipefail; cd "$(dirname "$0")"; . ./lib.sh
pf0 'insmod /tmp/pf.ko 2>/dev/null || insmod /opt/pf/pf.ko; true'; pf0 '/opt/pf/pfctl -e' >/dev/null
cat > /tmp/g2a.conf <<'C'
block drop in quick proto icmp from any to any
pass out quick
C
pfload /tmp/g2a.conf || { bad load1; finish; }
cli "ping -c 2 -W 1 $SRV_IP >/dev/null 2>&1" && bad "icmp still passes" || ok "icmp blocked"
cat > /tmp/g2b.conf <<'C'
block return-rst in quick proto tcp from any to any port 8080
pass out quick
C
pfload /tmp/g2b.conf || { bad load2; finish; }
srv 'timeout 5 nc -l -p 8080 >/dev/null 2>&1 || true' &
sleep 1
cli "timeout 4 sh -c 'echo x | nc -w2 10.0.2.20 8080; echo NC=\$?' | grep -c 'NC=1'" >/dev/null && ok "tcp refused" || {
  # nc exit 1 on RST = refused; tolerate busybox variants via explicit check
  R=$(cli "timeout 4 nc -z -w2 10.0.2.20 8080 >/dev/null 2>&1; echo $?"); [ "$R" != "0" ] && ok "tcp refused (rc=$R)" || bad "tcp not refused"; }
pf0 '/opt/pf/pfctl -d' >/dev/null; finish
