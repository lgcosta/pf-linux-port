#!/usr/bin/env bash
# M4 gate: pflog — logged packets must reach userspace.
#
# pflogd reads /dev/pflog (the port's chardev backend) and writes a pcap with
# linktype DLT_PFLOG (117).  A `log` rule + ICMP traffic must produce records
# that contain the real packet bytes (checked by searching the client address
# in the capture) and a valid pcap header.
set -uo pipefail; cd "$(dirname "$0")"; . ./lib.sh

pf0 'insmod /opt/pf/pf.ko 2>/dev/null || insmod /tmp/pf.ko; true'
pf0 '/opt/pf/pfctl -e; /opt/pf/pfctl -f /dev/null' >/dev/null
pf0 'pkill -f "[p]flogd" 2>/dev/null; rm -f /tmp/pflog.pcap; true'

LOGD=$(pfput ../../tools/out/pflogd) || { bad "pflogd deploy"; finish; }
pf0 "test -c /dev/pflog && echo DEV_OK" | grep -q DEV_OK || { bad "/dev/pflog missing"; finish; }
pf0 "chmod +x $LOGD; (setsid $LOGD -c 4 -f /dev/pflog /tmp/pflog.pcap >/tmp/pflogd.log 2>&1 </dev/null &); sleep 1; pgrep pflogd >/dev/null && echo DAEMON_UP" \
	| grep -q DAEMON_UP || { bad "pflogd start"; finish; }

cat > /tmp/g12.conf <<C
pass in quick log proto icmp from $CLI_IP keep state
pass out quick proto icmp keep state
C
pfload /tmp/g12.conf || { bad "log rule load"; finish; }

cli "ping -c3 -W2 $SRV_IP >/dev/null 2>&1"
sleep 2

# pcap header: magic d4c3b2a1 (LE) and network = 117 at offset 20
HDR=$(pf0 "od -An -tx1 -N24 /tmp/pflog.pcap 2>/dev/null | tr -d '[[:space:]]'")
if [ "${HDR:0:8}" = "d4c3b2a1" ]; then ok "pcap magic ok"; else bad "pcap header: $HDR"; fi
if [ "${HDR:40:4}" = "7500" ] || [ "${HDR:40:8}" = "75000000" ]; then
	ok "linktype DLT_PFLOG (117)"
else
	bad "linktype: ${HDR:40:8}"
fi

SZ=$(pf0 'wc -c < /tmp/pflog.pcap' | tr -d '[:space:]')
if [ "${SZ:-0}" -gt 24 ]; then ok "records captured ($SZ bytes)"; else bad "no records ($SZ)"; fi

# the client address must appear inside a captured packet
if pf0 "od -An -tx1 /tmp/pflog.pcap | tr -d '[[:space:]]' | grep -q 0a00010a"; then
	ok "captured payload contains $CLI_IP"
else
	bad "client address not in capture"
fi

pf0 'pkill pflogd 2>/dev/null; /opt/pf/pfctl -d' >/dev/null
finish
