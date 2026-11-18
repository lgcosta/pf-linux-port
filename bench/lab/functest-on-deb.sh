#!/usr/bin/env bash
# bench/lab/functest-on-deb.sh — functional coverage matrix, per backend.
#
# Runs ON the hypervisor, same path as the benchmark (bench-kedge ->
# bench-fw -> bench-srv).  One CSV row per (backend, feature, result).
#
# Results: pass | limited | n/a | fail — with the evidence in the note.
set -uo pipefail

OUT="${1:-/root/bench/functest}"
mkdir -p "$OUT"
CSV="$OUT/functional.csv"
printf 'backend,feature,result,note\n' > "$CSV"

FW=192.168.122.203
KEDGE=192.168.122.201
SRV=192.168.122.202
DCLI=10.0.0.2
DSRV=10.0.1.2
KEY=/root/.ssh/pflab_ed
NK="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=6"

g() { local ip="$1"; shift; ssh -i "$KEY" $NK "root@$ip" "$*"; }
push() { ssh -i "$KEY" $NK "root@$1" "cat > $2" < "$3"; }
row() { printf '%s,%s,%s,%s\n' "$@" >> "$CSV"; }

reset() { # forwarding must be ON: without it nothing enters the FORWARD
	# chain and every netfilter test silently "passes"
	local v i
	g "$FW" '/opt/pf/pfctl -d >/dev/null 2>&1; /opt/pf/pfctl -f /dev/null >/dev/null 2>&1;
		nft flush ruleset 2>/dev/null; iptables-nft -F 2>/dev/null; iptables-legacy -F 2>/dev/null;
		true' >/dev/null
	for i in 1 2 3; do
		g "$FW" 'sysctl -q -w net.ipv4.ip_forward=1; for f in /proc/sys/net/ipv4/conf/*/rp_filter; do echo 0 > $f; done' >/dev/null
		v=$(g "$FW" 'cat /proc/sys/net/ipv4/ip_forward' | tr -d '[:space:]')
		[ "$v" = "1" ] && return 0
		sleep 1
	done
	echo "functest: WARN ip_forward=${v:-?} on bench-fw" >&2
	return 1
}

ping_ok() { g "$KEDGE" "ping -c2 -W2 $DSRV >/dev/null 2>&1 && echo yes || echo no"; }

# --- per-backend ruleset helpers -------------------------------------------
load() { # load <backend> <file>
	local be="$1" f="$2"
	reset
	case "$be" in
	pf) push "$FW" /tmp/f.conf "$f"
	    g "$FW" 'insmod /opt/pf/pf.ko 2>/dev/null; /opt/pf/pfctl -e >/dev/null 2>&1;
	        /opt/pf/pfctl -f /tmp/f.conf >/dev/null 2>&1 && echo ok' ;;
	nft) push "$FW" /tmp/f.rules "$f"
	     g "$FW" 'nft -f /tmp/f.rules >/dev/null 2>&1 && echo ok' ;;
	ipt-nft) push "$FW" /tmp/f.rules "$f"
	     g "$FW" 'iptables-nft-restore < /tmp/f.rules >/dev/null 2>&1 && echo ok' ;;
	ipt-legacy) push "$FW" /tmp/f.rules "$f"
	     g "$FW" 'iptables-legacy-restore < /tmp/f.rules >/dev/null 2>&1 && echo ok' ;;
	esac
}

t=/tmp/functest
mkdir -p "$t"

# 1. IPv4 filtering: allow transit, then block it ---------------------------
cat > "$t/filter-pf" <<'C'
set skip on lo
pass quick on eth0
block in quick on eth1 proto icmp from 10.0.0.0/24 to 10.0.1.0/24
pass out quick on eth2 proto icmp
C
cat > "$t/filter-nft" <<'C'
flush ruleset
table inet b {
  chain f {
    type filter hook forward priority 0; policy accept;
    ip saddr 10.0.0.0/24 ip daddr 10.0.1.0/24 icmp type echo-request drop
  }
}
C
cat > "$t/filter-ipt" <<'C'
*filter
:INPUT ACCEPT [0:0]
:OUTPUT ACCEPT [0:0]
:FORWARD ACCEPT [0:0]
-A FORWARD -s 10.0.0.0/24 -d 10.0.1.0/24 -p icmp --icmp-type echo-request -j DROP
COMMIT
C

# 2. stateful: accept only established (a fresh ping must die) --------------
cat > "$t/state-pf" <<'C'
set skip on lo
pass quick on eth0
pass out quick on eth2 inet from 10.0.0.0/24 to 10.0.1.0/24 keep state
block in quick on eth1 inet from 10.0.0.0/24 to 10.0.1.0/24
C
cat > "$t/state-nft" <<'C'
flush ruleset
table inet b {
  chain f {
    type filter hook forward priority 0; policy drop;
    ct state established,related accept
  }
}
C
cat > "$t/state-ipt" <<'C'
*filter
:INPUT ACCEPT [0:0]
:OUTPUT ACCEPT [0:0]
:FORWARD DROP [0:0]
-A FORWARD -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT
COMMIT
C

# 3. SNAT out eth2 ----------------------------------------------------------
cat > "$t/nat-pf" <<'C'
set skip on lo
pass quick on eth0
pass out quick on eth2 from 10.0.0.0/24 to any nat-to (eth2)
pass in quick on eth1 inet from 10.0.0.0/24 to 10.0.1.0/24 keep state
C
cat > "$t/nat-nft" <<'C'
flush ruleset
table ip b {
  chain p {
    type nat hook postrouting priority 100; policy accept;
    ip saddr 10.0.0.0/24 oif eth2 masquerade
  }
}
C
cat > "$t/nat-ipt" <<'C'
*nat
:PREROUTING ACCEPT [0:0]
:POSTROUTING ACCEPT [0:0]
-A POSTROUTING -s 10.0.0.0/24 -o eth2 -j MASQUERADE
COMMIT
*filter
:INPUT ACCEPT [0:0]
:OUTPUT ACCEPT [0:0]
:FORWARD ACCEPT [0:0]
COMMIT
C

# 4. address sets: PF table / nft set / iptables (ipset unavailable?) -------
cat > "$t/set-pf" <<'C'
set skip on lo
pass quick on eth0
table <blk> persist { 10.0.0.2 }
block in quick on eth1 from <blk> to 10.0.1.0/24
pass out quick on eth2
C
cat > "$t/set-nft" <<'C'
flush ruleset
table inet b {
  set blk { type ipv4_addr }
  chain f {
    type filter hook forward priority 0; policy accept;
    ip saddr @blk ip daddr 10.0.1.0/24 drop
  }
}
C

for be in pf nft ipt-nft ipt-legacy; do
	suffix=rules
	case "$be" in pf) suffix=pf ;; nft) suffix=nft ;; *) suffix=ipt ;; esac

	# --- IPv4 filtering
	r=$(load "$be" "$t/filter-$suffix")
	if [ "$r" = ok ]; then
		if [ "$(ping_ok)" = no ]; then row "$be" ipv4_filter pass "ping blocked through FORWARD"
		else row "$be" ipv4_filter fail "ping still passed with a drop rule"; fi
	else row "$be" ipv4_filter n/a "ruleset did not load ($r)"; fi

	# --- stateful
	r=$(load "$be" "$t/state-$suffix")
	if [ "$r" = ok ]; then
		p=$(ping_ok)
		st=$(g "$FW" 'conntrack -L 2>/dev/null | wc -l')
		if [ "$p" = no ]; then row "$be" stateful pass "new flow dropped, conntrack entries=$st"
		else row "$be" stateful fail "new flow passed with policy drop"; fi
	else row "$be" stateful n/a "ruleset did not load ($r)"; fi

	# --- SNAT
	r=$(load "$be" "$t/nat-$suffix")
	if [ "$r" = ok ]; then
		g "$KEDGE" "ping -c2 -W2 $DSRV >/dev/null 2>&1" >/dev/null
		tr=$(g "$FW" 'conntrack -L 2>/dev/null | grep -c "10.0.1.1"')
		if [ "${tr:-0}" -ge 1 ]; then row "$be" snat pass "conntrack shows the egress translation ($tr entries)"
		else row "$be" snat limited "no translated entry seen (conntrack=$tr)"; fi
	else row "$be" snat n/a "ruleset did not load ($r)"; fi

	# --- address set
	if [ -f "$t/set-$suffix" ]; then
		r=$(load "$be" "$t/set-$suffix")
		if [ "$r" = ok ]; then
			if [ "$(ping_ok)" = no ]; then row "$be" addr_set pass "member address blocked from a set/table"
			else row "$be" addr_set fail "set member not blocked"; fi
		else row "$be" addr_set n/a "ruleset did not load ($r)"; fi
	else
		row "$be" addr_set n/a "no native set for this backend (iptables needs ipset, not built)"
	fi
done

# --- IPv6 (only where the family exists) -----------------------------------
if g "$FW" 'command -v ip6tables-legacy >/dev/null && echo yes' | grep -q yes; then
	reset
	g "$FW" 'ip6tables-legacy -A FORWARD -j DROP' >/dev/null 2>&1
	v6=$(g "$FW" 'ip6tables-legacy -S FORWARD 2>/dev/null | head -1')
	row ipt-legacy ipv6_filter pass "rule installed: $v6"
	g "$FW" 'ip6tables-legacy -F FORWARD' >/dev/null 2>&1
fi
reset
printf 'functest: %s rows -> %s\n' "$(($(wc -l < "$CSV") - 1))" "$CSV"
