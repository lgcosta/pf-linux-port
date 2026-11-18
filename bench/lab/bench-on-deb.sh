#!/usr/bin/env bash
# bench/lab/bench-on-deb.sh — runs ON the hypervisor (deb).
#
# Single hop to every guest (deb -> 192.168.122.x), so no nested quoting.
# Measures the bench-kedge -> bench-fw -> bench-srv path with one backend
# active at a time and writes a CSV of raw samples to $OUT/results.csv.
#
#   bench-on-deb.sh <outdir> [reps] [secs] [streams] [scenarios] [backends]
set -uo pipefail

OUT="${1:?usage: bench-on-deb.sh <outdir> [reps] [secs] [streams] [scenarios] [backends]}"
REPS="${2:-5}"; SECS="${3:-10}"; STREAMS="${4:-4}"
RATE="${BENCH_RATE:-200M}"
SCENARIOS="${5:-stateless stateful nat scale reload}"
BACKENDS="${6:-baseline pf nft ipt-nft ipt-legacy}"
# argument sanity: scenarios/backends are space-separated lists, the rest
# are numbers - a swapped argument produced `iperf3 -t stateless ...` once.
for n in "$REPS" "$SECS" "$STREAMS"; do
	case "$n" in ''|*[!0-9]*) echo "bench: bad numeric argument '$n'" >&2; exit 2 ;; esac
done
SCALE_N="${BENCH_SCALE_RULES:-200}"
RELOADS="${BENCH_RELOADS:-20}"

FW=192.168.122.203
KEDGE=192.168.122.201
SRV=192.168.122.202
DCLI=10.0.0.2
DSRV=10.0.1.2

KEY=/root/.ssh/pflab_ed
NK="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=6"
RULES=/root/bench/rulesets

mkdir -p "$OUT"
CSV="$OUT/results.csv"
printf 'scenario,backend,rep,metric,value,unit,offered\n' > "$CSV"

g() { # g <ip> <command...> — run on a guest (single hop, hard timeout)
	local ip="$1"; shift
	timeout 25 ssh -i "$KEY" $NK "root@$ip" "$*"
}

fw_restart() { # a wedged PF leaves the guest unresponsive; start clean
	virsh destroy bench-fw >/dev/null 2>&1
	virsh start bench-fw >/dev/null 2>&1
	local i
	for i in $(seq 1 12); do
		sleep 5
		timeout 8 ssh -i "$KEY" $NK "root@192.168.122.203" true 2>/dev/null && return 0
	done
	return 1
}
push() { # push <ip> <remote> <local-file>
	ssh -i "$KEY" $NK "root@$1" "cat > $2" < "$3"
}
emit() { printf '%s,%s,%s,%s,%s,%s,%s\n' "$@" >> "$CSV"; }
say() { printf 'bench: %s\n' "$*" >&2; }
json() { # json <file> <key> — last numeric occurrence of <key> in the JSON
	python3 /root/bench/lab/jsonfield.py "$1" "$2" 2>/dev/null
}

prepare_fw() {
	local v i
	for i in 1 2 3; do
		g "$FW" 'sysctl -q -w net.ipv4.ip_forward=1; for f in /proc/sys/net/ipv4/conf/*/rp_filter; do echo 0 > $f; done; true' >/dev/null
		v=$(g "$FW" 'cat /proc/sys/net/ipv4/ip_forward' | tr -d "[:space:]")
		[ "$v" = "1" ] && return 0
		sleep 2
	done
	say "WARN: could not enable ip_forward on bench-fw (value=${v:-?})"
	return 1
}

quiet_all() {
	g "$FW" '/opt/pf/pfctl -d >/dev/null 2>&1; /opt/pf/pfctl -f /dev/null >/dev/null 2>&1;
		nft flush ruleset 2>/dev/null; iptables-nft -F 2>/dev/null; iptables-legacy -F 2>/dev/null; true' >/dev/null
}

apply() { # apply <backend> <scenario>
	local be="$1" sc="$2" f
	[ -f "$RULES/$sc-pf.conf" ] || [ -f "$RULES/$sc-nft.rules" ] || sc=stateless
	case "$be" in
	baseline) quiet_all; return 0 ;;
	pf)        fw_restart || say "WARN: bench-fw did not come back"
	           f="$RULES/$sc-pf.conf"; push "$FW" /tmp/r.conf "$f"; prepare_fw
	           g "$FW" 'rmmod pf 2>/dev/null; insmod /opt/pf/pf.ko 2>/dev/null;
	               /opt/pf/pfctl -e >/dev/null 2>&1; /opt/pf/pfctl -f /tmp/r.conf >/dev/null 2>&1 && echo RULES_OK' ;;
	nft)       f="$RULES/$sc-nft.rules"; push "$FW" /tmp/r.rules "$f"; prepare_fw
	           g "$FW" 'nft -f /tmp/r.rules >/dev/null 2>&1 && echo RULES_OK' ;;
	ipt-nft)   f="$RULES/$sc-ipt.rules"; push "$FW" /tmp/r.rules "$f"; prepare_fw
	           g "$FW" 'iptables-nft-restore < /tmp/r.rules >/dev/null 2>&1 && echo RULES_OK' ;;
	ipt-legacy) f="$RULES/$sc-ipt.rules"; push "$FW" /tmp/r.rules "$f"; prepare_fw
	           g "$FW" 'iptables-legacy-restore < /tmp/r.rules >/dev/null 2>&1 && echo RULES_OK' ;;
	*) say "unknown backend $be"; return 1 ;;
	esac
}

iperf_srv() { # start the server and wait until it listens (a race here
# shows up as a 0 Gbit/s cell on a backend that is fine)
	g "$SRV" 'pkill -x iperf3 2>/dev/null; iperf3 -s -D >/dev/null 2>&1
		for i in $(seq 1 40); do
			ss -lnt 2>/dev/null | grep -q ":5201" && exit 0
			sleep 0.25
		done; exit 1' >/dev/null
}

thr() { # thr <scenario> <backend> <rep> <streams>
	local sc="$1" be="$2" rep="$3" p="${4:-$STREAMS}"
	iperf_srv
	local j="$OUT/raw-$sc-$be-$rep-iperf.json"
	timeout 40 ssh -i "$KEY" $NK "root@$KEDGE" "iperf3 -c $DSRV -t $SECS -P $p -b $RATE -i 0 -J 2>/dev/null" > "$j" 2>/dev/null
	local b; b=$(json "$j" bits_per_second | tail -1)
	emit "$sc" "$be" "$rep" tcp_gbit "$(awk -v x="${b:-0}" 'BEGIN{printf "%.3f", x/1e9}')" Gbit/s "tcp -P$p -b$RATE -t$SECS"
}

udp() { # udp <scenario> <backend> <rep>
	local sc="$1" be="$2" rep="$3"
	iperf_srv
	local j="$OUT/raw-$sc-$be-$rep-udp.json"
	timeout 40 ssh -i "$KEY" $NK "root@$KEDGE" "iperf3 -c $DSRV -u -b 200M -t $SECS -l 1400 -i 0 -J 2>/dev/null" > "$j" 2>/dev/null
	local pk sec pps
	pk=$(json "$j" packets); sec=$(json "$j" seconds)
	pps=$(awk -v p="${pk:-0}" -v t="${sec:-0}" 'BEGIN{ if (t > 0) printf "%.0f", p/t; else print 0 }')
	emit "$sc" "$be" "$rep" udp_pps "$pps" pps "udp -b200M -l1400 -t$SECS"
	emit "$sc" "$be" "$rep" udp_loss "$(json "$j" lost_percent)" percent "udp -b200M -l1400 -t$SECS"
}

conns() { # conns <scenario> <backend> <rep>
	local sc="$1" be="$2" rep="$3"
	local n
	n=$(g "$KEDGE" "python3 -c \"
import socket,time
t0=time.time(); n=0
while time.time()-t0 < $SECS:
    s=socket.socket(); s.settimeout(0.3)
    try: s.connect(('$DSRV',22)); n+=1
    except OSError: pass
    s.close()
print(n/(time.time()-t0))
\"")
	emit "$sc" "$be" "$rep" conn_per_s "${n:-0}" conn/s "tcp connect/close -t$SECS"
}

lat() { # lat <scenario> <backend> <rep>
	local sc="$1" be="$2" rep="$3"
	local j="$OUT/raw-$sc-$be-$rep-ping.txt"
	timeout 40 ssh -i "$KEY" $NK "root@$KEDGE" "ping -i 0.01 -c 200 -W 2 $DSRV 2>/dev/null | sed -n 's/.*time=//; s/ ms//p'" > "$j" 2>/dev/null
	local p; p=$(sort -n "$j" | awk '{a[NR]=$1} END{if(NR==0){print "0 0 0"; exit} print a[int(NR*0.50)+1], a[int(NR*0.95)+1], a[int(NR*0.99)+1]}')
	set -- $p
	emit "$sc" "$be" "$rep" latency_p50 "${1:-0}" ms "ping 200x10ms"
	emit "$sc" "$be" "$rep" latency_p95 "${2:-0}" ms "ping 200x10ms"
	emit "$sc" "$be" "$rep" latency_p99 "${3:-0}" ms "ping 200x10ms"
}

softirq() { # softirq <scenario> <backend> <rep> — firewall CPU time (ticks)
	local sc="$1" be="$2" rep="$3"
	local t; t=$(g "$FW" 'awk "{print \$2+\$4}" /proc/stat | head -1')
	emit "$sc" "$be" "$rep" fw_cpu_ticks "${t:-0}" ticks "cumulative user+softirq"
}

# --- rule-scale rulesets (generated here, pushed to the firewall) -----------
gen_scale() { # gen_scale <backend> <position>
	local be="$1" pos="$2" f="$OUT/scale-$be-$pos.rules" i=1
	case "$be" in
	nft) {
		echo "flush ruleset"
		echo "table inet bench { chain forward { type filter hook forward priority 0; policy accept;"
		echo "  ct state established,related counter accept"
		[ "$pos" = start ] && echo "  ip saddr $DCLI ip daddr $DSRV counter accept"
		while [ $i -le "$SCALE_N" ]; do
			echo "  ip daddr 10.9.$((i % 250)).$((i % 250 + 1)) counter drop"
			[ "$pos" = middle ] && [ $i -eq $((SCALE_N / 2)) ] && echo "  ip saddr $DCLI ip daddr $DSRV counter accept"
			i=$((i+1))
		done
		[ "$pos" = end ] && echo "  ip saddr $DCLI ip daddr $DSRV counter accept"
		echo "} }"
	} > "$f" ;;
	ipt-*) {
		echo "*filter"; echo ":INPUT ACCEPT [0:0]"; echo ":OUTPUT ACCEPT [0:0]"; echo ":FORWARD ACCEPT [0:0]"
		echo "-A FORWARD -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT"
		[ "$pos" = start ] && echo "-A FORWARD -s $DCLI -d $DSRV -j ACCEPT"
		while [ $i -le "$SCALE_N" ]; do
			echo "-A FORWARD -d 10.9.$((i % 250)).$((i % 250 + 1)) -j DROP"
			[ "$pos" = middle ] && [ $i -eq $((SCALE_N / 2)) ] && echo "-A FORWARD -s $DCLI -d $DSRV -j ACCEPT"
			i=$((i+1))
		done
		[ "$pos" = end ] && echo "-A FORWARD -s $DCLI -d $DSRV -j ACCEPT"
		echo "COMMIT"
	} > "$f" ;;
	pf) {
		echo "set skip on lo"
		echo "pass quick on eth0"
		echo "pass out quick on eth2 inet from 10.0.0.0/24 to 10.0.1.0/24 keep state"
		echo "pass in quick on eth1 inet from 10.0.0.0/24 to 10.0.1.0/24 keep state"
		[ "$pos" = start ] && echo "pass in quick on eth1 from $DCLI to $DSRV keep state"
		while [ $i -le "$SCALE_N" ]; do
			echo "block drop in quick on eth1 from any to 10.9.$((i % 250)).$((i % 250 + 1))"
			[ "$pos" = middle ] && [ $i -eq $((SCALE_N / 2)) ] && echo "pass in quick on eth1 from $DCLI to $DSRV keep state"
			i=$((i+1))
		done
		[ "$pos" = end ] && echo "pass in quick on eth1 from $DCLI to $DSRV keep state"
	} > "$f" ;;
	esac
	echo "$f"
}

apply_file() { # apply_file <backend> <file>
	local be="$1" f="$2"
	quiet_all; prepare_fw
	case "$be" in
	pf)  push "$FW" /tmp/r.conf "$f"; g "$FW" 'rmmod pf 2>/dev/null; insmod /opt/pf/pf.ko 2>/dev/null; /opt/pf/pfctl -e >/dev/null 2>&1; /opt/pf/pfctl -f /tmp/r.conf >/dev/null 2>&1 || echo LOADFAIL' ;;
	nft) push "$FW" /tmp/r.rules "$f"; g "$FW" 'nft -f /tmp/r.rules >/dev/null 2>&1 || echo LOADFAIL' ;;
	ipt-nft) push "$FW" /tmp/r.rules "$f"; g "$FW" 'iptables-nft-restore < /tmp/r.rules >/dev/null 2>&1 || echo LOADFAIL' ;;
	ipt-legacy) push "$FW" /tmp/r.rules "$f"; g "$FW" 'iptables-legacy-restore < /tmp/r.rules >/dev/null 2>&1 || echo LOADFAIL' ;;
	esac
}

# --- main -------------------------------------------------------------------
for sc in $SCENARIOS; do
	for rep in $(seq 1 "$REPS"); do
		if [ $((rep % 2)) -eq 1 ]; then order="$BACKENDS"
		else order=$(printf '%s\n' $BACKENDS | tac | tr '\n' ' '); fi
		for be in $order; do
			say "scenario=$sc backend=$be rep=$rep"
			case "$sc" in
			scale)
				for pos in start middle end; do
					f=$(gen_scale "$be" "$pos")
					if [ "$be" = baseline ]; then quiet_all; prepare_fw
					else apply_file "$be" "$f"; fi
					iperf_srv
					local_j="$OUT/raw-scale-$be-$pos-$rep.json"
					timeout 40 ssh -i "$KEY" $NK "root@$KEDGE" "iperf3 -c $DSRV -t $SECS -P 1 -b $RATE -i 0 -J 2>/dev/null" > "$local_j" 2>/dev/null
					b=$(json "$local_j" bits_per_second | tail -1)
					emit "scale-$pos" "$be" "$rep" tcp_gbit "$(awk -v x="${b:-0}" 'BEGIN{printf "%.3f", x/1e9}')" Gbit/s "$SCALE_N rules, hit at $pos"
				done ;;
			ceiling)
				case "$be" in
				pf) emit ceiling pf "$rep" tcp_gbit "" Gbit/s "unbounded (not measurable: PF wedges above ~1 Gbit/s)"
				    continue ;;
				esac
				apply "$be" "$sc" >/dev/null 2>&1
				prepare_fw
				iperf_srv
				local_j="$OUT/raw-ceiling-$be-$rep.json"
				timeout 40 ssh -i "$KEY" $NK "root@$KEDGE" "iperf3 -c $DSRV -t $SECS -P $STREAMS -i 0 -J 2>/dev/null" > "$local_j" 2>/dev/null
				b=$(json "$local_j" bits_per_second | tail -1)
				emit ceiling "$be" "$rep" tcp_gbit "$(awk -v x="${b:-0}" 'BEGIN{printf "%.3f", x/1e9}')" Gbit/s "unbounded -P$STREAMS -t$SECS" ;;
			reload)
				apply "$be" "$sc" >/dev/null
				iperf_srv
				g "$KEDGE" "iperf3 -c $DSRV -t 14 -P 1 -J > /tmp/ipert.json 2>/dev/null &" >/dev/null
				sleep 2
				t0=$(g "$FW" 'awk "{print \$1}" /proc/uptime')
				i=0; while [ $i -lt "$RELOADS" ]; do apply "$be" "$sc" >/dev/null; i=$((i+1)); done
				t1=$(g "$FW" 'awk "{print \$1}" /proc/uptime')
				emit reload "$be" "$rep" apply_ms "$(awk -v a="$t0" -v b="$t1" -v n="$RELOADS" 'BEGIN{printf "%.1f", (b-a)*1000/n}')" ms "$RELOADS reloads under a TCP stream"
				sleep 12
				j="$OUT/raw-reload-$be-$rep.json"
				g "$KEDGE" 'cat /tmp/ipert.json' > "$j"
				emit reload "$be" "$rep" loss "$(json "$j" lost_percent)" percent "$RELOADS reloads under a TCP stream" ;;
			*)
				apply "$be" "$sc" >/dev/null
				prepare_fw
				g "$KEDGE" "ping -c 2 -W 1 $DSRV >/dev/null 2>&1; true" >/dev/null
				softirq "$sc" "$be" "$rep"
				thr "$sc" "$be" "$rep"
				lat "$sc" "$be" "$rep"
				case "$sc" in
				stateless) udp "$sc" "$be" "$rep" ;;
				stateful)  conns "$sc" "$be" "$rep" ;;
				esac ;;
			esac
		done
	done
done
quiet_all
prepare_fw
say "done: $CSV"
