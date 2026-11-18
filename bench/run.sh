#!/usr/bin/env bash
# bench/run.sh — build-host wrapper around bench/lab/bench-on-deb.sh.
#
# Pushes the driver and the rulesets to the hypervisor, runs the sweep there
# (one ssh hop per guest, no nested quoting) and pulls the CSV + raw samples
# back into bench/data/run-<ts>/.
#
#   ./bench/run.sh
#   BENCH_REPS=2 BENCH_SECS=5 BENCH_SCENARIOS="stateless" ./bench/run.sh
set -uo pipefail
BENCH="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEB="${DEB:-root@192.168.1.242}"

REPS="${BENCH_REPS:-5}"
SECS="${BENCH_SECS:-10}"
STREAMS="${BENCH_STREAMS:-4}"
SCENARIOS="${BENCH_SCENARIOS:-stateless stateful nat scale reload}"
BACKENDS="${BENCH_BACKENDS:-baseline pf nft ipt-nft ipt-legacy}"

TS="$(date +%Y%m%d-%H%M%S)"
OUT="$BENCH/data/run-$TS"
mkdir -p "$OUT"

echo "bench: pushing driver + rulesets to $DEB"
ssh -o BatchMode=yes "$DEB" 'mkdir -p /root/bench/rulesets /root/bench/lab'
rsync -a -e "ssh -o BatchMode=yes" "$BENCH/rulesets/" "$DEB":/root/bench/rulesets/
rsync -a -e "ssh -o BatchMode=yes" "$BENCH/lab/bench-on-deb.sh" "$BENCH/lab/jsonfield.py" "$DEB":/root/bench/lab/

echo "bench: sweep reps=$REPS secs=$SECS streams=$STREAMS"
echo "bench: scenarios=$SCENARIOS"
echo "bench: backends=$BACKENDS"
ssh -o BatchMode=yes "$DEB" \
	"BENCH_SCALE_RULES=${BENCH_SCALE_RULES:-200} BENCH_RELOADS=${BENCH_RELOADS:-20} \
	 bash /root/bench/lab/bench-on-deb.sh /root/bench/out $REPS $SECS $STREAMS '$SCENARIOS' '$BACKENDS'" \
	2>&1 | tail -40

echo "bench: pulling results"
rsync -a -e "ssh -o BatchMode=yes" "$DEB":/root/bench/out/ "$OUT"/
echo "bench: $OUT/results.csv ($(wc -l < "$OUT/results.csv" 2>/dev/null || echo 0) lines)"
