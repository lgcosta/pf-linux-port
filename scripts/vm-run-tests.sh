#!/usr/bin/env bash
# VM suite runner with deterministic per-gate VM reset.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
DEB="${DEB:-root@192.168.1.242}"
LOGD="$REPO/out/test-logs"; mkdir -p "$LOGD"
PF0_IP=192.168.122.8
SSH="ssh -o StrictHostKeyChecking=accept-new -o ConnectTimeout=8 -i /root/.ssh/pflab_ed root@$PF0_IP"

reset_pf0() {
	ssh "$DEB" "virsh destroy pftest-pf0 >/dev/null 2>&1; virsh start pftest-pf0 >/dev/null"
	local i
	for i in $(seq 1 15); do
		sleep 6
		ssh "$DEB" "$SSH true" 2>/dev/null && return 0
	done
	return 1
}
deploy() {
	scp -q -o ProxyJump=root@192.168.1.242 -o StrictHostKeyChecking=accept-new \
		-i "$HOME/.ssh/pflab_ed" "$REPO/module/pf.ko" \
		"$REPO/tools/out/pfctl" "$REPO/tests/confs/"*.conf pf0:/tmp/ 2>/dev/null || \
	ssh "$DEB" "mkdir -p /opt/pf"
	# stage onto pf0 /opt/pf via mgmt alias already copied to /tmp
	ssh "$DEB" "$SSH 'mkdir -p /opt/pf; mv /tmp/pf.ko /tmp/pfctl /opt/pf/ 2>/dev/null; true'" >/dev/null
}
PUSH=0 FAIL=0
for t in "$REPO"/tests/vm/[0-9]*.sh; do
	[ -e "$t" ] || continue
	name="$(basename "$t")"
	echo "=== $name ==="
	reset_pf0 || { echo "FAIL $name (vm reset)"; FAIL=$((FAIL+1)); continue; }
	deploy
	if bash "$t" >"$LOGD/$name.log" 2>&1; then
		echo "PASS $name"; PUSH=$((PUSH+1))
	else
		echo "FAIL $name (rc=$?)"; FAIL=$((FAIL+1))
	fi
	sed -e 's/^/    /' "$LOGD/$name.log" | grep -E '^(OK|NOT OK)' | head -8 | sed 's/^ /  /'
done
echo "----------------------------------------"
echo "PASS=$PUSH FAIL=$FAIL"
[ $FAIL -eq 0 ]
