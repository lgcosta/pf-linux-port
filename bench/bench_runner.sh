#!/usr/bin/env bash
# bench/bench_runner.sh — one measurement sweep: apply a backend's ruleset,
# drive traffic, collect numbers, tear the ruleset down.  Results land in
# bench/data/run-<ts>/ as one CSV row per (scenario, backend, repetition).
set -uo pipefail
BENCH_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$BENCH_ROOT/env.sh"

SECS="${BENCH_SECS:-10}"
REPS="${BENCH_REPS:-5}"
STREAMS="${BENCH_STREAMS:-4}"
BACKENDS="${BENCH_BACKENDS:-pf nft ipt-nft ipt-legacy}"
SCENARIOS="${BENCH_SCENARIOS:-stateless stateful scale reload}"
CSV="$RUN_DIR/results.csv"
[ -f "$CSV" ] || printf 'run,scenario,backend,rep,metric,value,unit\n' > "$CSV"

# --- helper: run iperf3 and emit one row -----------------------------------
run_iperf() { # run_iperf <scenario> <backend> <rep> <direction>
	local sc="$1" be="$2" rep="$3" dir="$4"
	# client runs on kedge, server on srv
	local out
	out=$(on_kedge "iperf3 -c $SRV_IP -t $SECS -P $STREAMS -i 0 -J 2>/dev/null")
	local gbps
	gbps=$(printf '%s' "$out" | jq -r ".end.sum_received.bits_per_second / 1e9" 2>/dev/null)
	local retre trans
	retre=$(printf '%s' "$out" | jq -r ".end.sum_sent.retransmits // 0" 2>/dev/null)
	trans=$(printf '%s' "$out" | jq -r ".end.sum_sent.tr // 0" 2>/dev/null)
	printf '%s,%s,%d,%s,%s,%s,Gbit/s\n' "$RUN_STAMP" "$sc" "$be" "$rep" "$dir" "$gbps" >> "$CSV"
}

# --- helper: run conntrack rate ---------------------------------------------
run_cps() { # run_cps <scenario> <backend> <rep>
	local sc="$1" be="$2" rep="$3"
	local out
	out=$(on_kedge "python3 -c '
import socket, time
t0=time.time()
n=0
while time.time()-t0 < ${SECS}:
    s=socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(0.5)
    try:
        s.connect((\"$SRV_IP\", 5201)); n+=1
    except OSError: pass
    s.close()
print(n/(time.time()-t0))
'")
	printf '%s,%s,%d,%s,%s,%s,conn/s\n' "$RUN_STAMP" "$sc" "$be" "$rep" "cps" "${out:-0}" >> "$CSV"
}
