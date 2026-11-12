#!/usr/bin/env bash
# run-stress.sh — drive the PF stress test (tests/vm/16-stress.sh) against the
# libvirt lab and turn the metrics into a report.
#
#   ./scripts/stress/run-stress.sh              # default profile
#   ./scripts/stress/run-stress.sh --quick      # smaller profile (smoke)
#   ./scripts/stress/run-stress.sh --seconds 30 --connections 1000
#
# Output: out/stress/<timestamp>/report.md (+ the raw gate log).
# Exit status: 0 when every stress phase passed, 1 otherwise.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
DEB="${DEB:-root@192.168.1.242}"
PF0_IP=192.168.122.8
TS="$(date +%Y%m%d-%H%M%S)"
OUTD="$REPO/out/stress/$TS"
mkdir -p "$OUTD"

SECS=10
PAR=4
RATE=100M
CONNS=300
RELOADS=200
RULES=200
TENTRIES=2000
MAXOH=30

while [ $# -gt 0 ]; do
	case "$1" in
	--quick)     SECS=5; CONNS=100; RELOADS=50; RULES=50; TENTRIES=500; shift ;;
	--seconds)   SECS=$2; shift 2 ;;
	--parallel)  PAR=$2; shift 2 ;;
	--rate)      RATE=$2; shift 2 ;;
	--connections) CONNS=$2; shift 2 ;;
	--reloads)   RELOADS=$2; shift 2 ;;
	--rules)     RULES=$2; shift 2 ;;
	--table-entries) TENTRIES=$2; shift 2 ;;
	--max-overhead) MAXOH=$2; shift 2 ;;
	-h|--help)   sed -n '2,12p' "$0"; exit 0 ;;
	*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
done

SSH="ssh -o StrictHostKeyChecking=accept-new -o ConnectTimeout=8 -i /root/.ssh/pflab_ed root@$PF0_IP"

echo "pflab stress: profile seconds=$SECS parallel=$PAR rate/stream=$RATE connections=$CONNS reloads=$RELOADS rules=$RULES table_entries=$TENTRIES max_overhead=$MAXOH%"

echo "pflab stress: resetting pftest-pf0"
ssh "$DEB" "virsh destroy pftest-pf0 >/dev/null 2>&1; virsh start pftest-pf0 >/dev/null" || {
	echo "pflab stress FAIL: cannot restart pftest-pf0" >&2; exit 1; }
up=0
for i in $(seq 1 20); do
	sleep 6
	if ssh "$DEB" "$SSH true" 2>/dev/null; then up=1; break; fi
done
[ "$up" = 1 ] || { echo "pflab stress FAIL: pf0 never came back" >&2; exit 1; }

echo "pflab stress: deploying pf.ko + pfctl to pf0:/opt/pf"
for i in 1 2 3; do
	if scp -q -o ProxyJump=root@192.168.1.242 -o StrictHostKeyChecking=accept-new \
		-i "$HOME/.ssh/pflab_ed" "$REPO/module/pf.ko" "$REPO/tools/out/pfctl" \
		"$REPO/tools/out/pdivert" "$REPO/tools/out/pflogd" pf0:/tmp/ 2>/dev/null; then
		break
	fi
	sleep 2
done
ssh "$DEB" "$SSH 'mkdir -p /opt/pf; mv /tmp/pf.ko /tmp/pfctl /tmp/pdivert /tmp/pflogd /opt/pf/ 2>/dev/null; chmod +x /opt/pf/pfctl 2>/dev/null; true'" >/dev/null
ssh "$DEB" "$SSH 'test -s /opt/pf/pf.ko && test -x /opt/pf/pfctl'" >/dev/null 2>&1 || {
	echo "pflab stress FAIL: pf.ko/pfctl missing on pf0" >&2; exit 1; }

echo "pflab stress: running the stress gate"
# The guest's serial console is a pty: a panic or a hang is lost when the VM
# reboots.  Capture it on the hypervisor (the redirect happens *there*) and
# copy it back when the run ends.
REMOTE_CONSOLE="/tmp/pf0-console-$TS.log"
ssh "$DEB" "PTY=\$(virsh ttyconsole pftest-pf0); setsid nohup timeout 1800 cat \$PTY >$REMOTE_CONSOLE 2>&1 </dev/null &" >/dev/null 2>&1
sleep 2
export STRESS_SECS="$SECS" STRESS_PARALLEL="$PAR" STRESS_RATE="$RATE" STRESS_CONNS="$CONNS" \
	STRESS_RELOADS="$RELOADS" STRESS_RULES="$RULES" \
	STRESS_TABLE_ENTRIES="$TENTRIES" STRESS_MAX_OVERHEAD_PCT="$MAXOH"
bash "$REPO/tests/vm/16-stress.sh" >"$OUTD/16-stress.sh.log" 2>&1
RC=$?

# --- report ----------------------------------------------------------------
scp -q -o BatchMode=yes -o StrictHostKeyChecking=accept-new "$DEB:$REMOTE_CONSOLE" \
	"$OUTD/pf0-console.log" 2>/dev/null || true
{
	echo "# PF-on-Linux — stress report"
	echo
	echo "- date: $(date -Is)"
	echo "- profile: seconds=$SECS parallel=$PAR rate/stream=$RATE connections=$CONNS reloads=$RELOADS rules=$RULES table_entries=$TENTRIES max_overhead=${MAXOH}%"
	echo "- exit status: $RC"
	echo "- guest serial console: \`pf0-console.log\` (kernel messages during the run)"
	echo
	echo "## Phases"
	echo
	echo '```'
	grep '^STRESS ' "$OUTD/16-stress.sh.log" || echo "(no metrics captured)"
	echo '```'
	echo
	echo "## Assertions"
	echo
	echo '```'
	grep -E '^(OK|NOT OK)' "$OUTD/16-stress.sh.log" || echo "(no assertions)"
	echo '```'
} >"$OUTD/report.md"

echo
echo "pflab stress: report -> out/stress/$TS/report.md"
grep '^STRESS ' "$OUTD/16-stress.sh.log" || true
echo
if [ $RC -eq 0 ]; then
	echo "STRESS RESULT: PASS"
else
	echo "STRESS RESULT: FAIL (rc=$RC)"
	sed -n '/^NOT OK/p' "$OUTD/16-stress.sh.log" | sed 's/^/  /'
fi
exit $RC
