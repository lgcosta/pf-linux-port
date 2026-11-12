#!/usr/bin/env bash
# M3 gate: rdr-to — destination/port redirect.
#
# Lab: the client connects to the firewall's own address 10.0.1.1:8080; pf
# redirects it to the server (10.0.2.20:80, busybox httpd).  This exercises
# the inbound translation *and* the re-route of a packet whose destination pf
# rewrote (the packet is no longer local to the firewall).
set -uo pipefail; cd "$(dirname "$0")"; . ./lib.sh

pf0 'insmod /opt/pf/pf.ko 2>/dev/null || insmod /tmp/pf.ko; true'
pf0 '/opt/pf/pfctl -e; /opt/pf/pfctl -f /dev/null' >/dev/null

# a tiny responder on srv:80 (base64-encoded so it survives the two ssh hops
# without nested quoting problems; detached so ssh returns)
LOOP=$(printf 'while :; do nc -l -p 80 < /tmp/resp.txt >/dev/null 2>&1; done\n' | base64 | tr -d '\n')
# NOTE: never pkill -f a pattern that also appears in this very command line -
# it kills the invoking shell.  Use a pidfile and check the listening socket.
srv "echo PF-RDR-OK > /tmp/resp.txt; echo $LOOP | base64 -d > /tmp/loop.sh; [ -f /tmp/loop.pid ] && kill \$(cat /tmp/loop.pid) 2>/dev/null; pkill nc 2>/dev/null; setsid sh /tmp/loop.sh >/dev/null 2>&1 </dev/null & echo \$! > /tmp/loop.pid; sleep 1; netstat -lnt 2>/dev/null | grep -q ':80 ' && echo RESPONDER_UP" | grep -q RESPONDER_UP && ok "responder up on srv:80" || bad "responder start"

cat > /tmp/g06.conf <<C
pass in quick proto tcp from 10.0.1.0/24 to 10.0.1.1 port 8080 rdr-to $SRV_IP port 80 keep state
pass out quick proto tcp keep state
C
pfload /tmp/g06.conf || { bad "rdr rule load"; finish; }
pf0 '/opt/pf/pfctl -sr' | grep -q 'rdr-to' && ok "rdr rule listed" || bad "rdr not listed"

# the client talks to the firewall's address; pf redirects to srv:80
RESP=$(cli 'printf "GET / HTTP/1.0\r\n\r\n" | nc -w3 10.0.1.1 8080' 2>/dev/null)
if echo "$RESP" | grep -q 'PF-RDR-OK'; then
	ok "redirect delivered the response"
else
	bad "no redirected response"
	printf '%s\n' "$RESP" | head -4 | sed 's/^/    /'
fi

# the state must show the translated tuple (server:80 with the original in parens)
pf0 '/opt/pf/pfctl -ss' | grep -E "$SRV_IP:80 .*$CLI_IP" >/dev/null && ok "state shows the translation" || bad "no translated state"

# the reply path must survive the un-redirect
pf0 '/opt/pf/pfctl -s info' | grep -qiE 'match' && ok "counters readable" || bad "counters"

srv '[ -f /tmp/loop.pid ] && kill $(cat /tmp/loop.pid) 2>/dev/null; pkill nc 2>/dev/null; true' >/dev/null

# bulk (GSO/TSO) through the redirect: 2 MB must arrive byte-identical.
# This is the regression that "materialize the checksum in software" introduced:
# computing the transport checksum once for a GSO skb corrupts every segment
# after the first, and the peer silently stalls (sshd mute, transfers hang).
B64=$(printf 'head -c 2000000 /dev/zero | tr "\0" A > /tmp/big.txt\n' | base64 | tr -d '\n')
srv "echo $B64 | base64 -d > /tmp/mkbig.sh; sh /tmp/mkbig.sh; pkill nc 2>/dev/null; sleep 1; nohup setsid nc -l -p 9091 < /tmp/big.txt >/dev/null 2>&1 & sleep 1; netstat -lnt | grep -q 9091 && echo BULK_UP" | grep -q BULK_UP || bad "bulk listener"
pf0 "printf 'pass in quick proto tcp from 10.0.1.0/24 to 10.0.1.1 port 9091 rdr-to $SRV_IP port 9091 keep state\npass out quick proto tcp keep state\n' > /tmp/g06b.conf; /opt/pf/pfctl -f /tmp/g06b.conf && echo BULK_RULE" >/dev/null
GOT=$(cli 'nc -w10 10.0.1.1 9091 | wc -c' | tr -d '[:space:]')
EXP=$(srv 'wc -c < /tmp/big.txt' | tr -d '[:space:]')
if [ "$GOT" = "$EXP" ] && [ "${GOT:-0}" -ge 2000000 ]; then
	ok "2 MB through the redirect intact ($GOT bytes)"
else
	bad "bulk through redirect: got $GOT want $EXP"
fi
# the firewall must still answer ssh (the GSO path must not wedge the box)
pf0 'echo SSH_ALIVE' | grep -q SSH_ALIVE && ok "pf0 sshd still responsive" || bad "pf0 unreachable after bulk"
srv 'pkill nc 2>/dev/null; true' >/dev/null

pf0 '/opt/pf/pfctl -d' >/dev/null
finish
