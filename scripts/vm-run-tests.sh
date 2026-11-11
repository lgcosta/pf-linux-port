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
	# The scp through the libvirt jump host is occasionally refused; a silent
	# failure used to leave the previous gate's module/pfctl in place, which
	# showed up as a bogus gate failure.  Retry and verify the copy.
	local i
	for i in 1 2 3; do
		if scp -q -o ProxyJump=root@192.168.1.242 -o StrictHostKeyChecking=accept-new \
			-i "$HOME/.ssh/pflab_ed" "$REPO/module/pf.ko" \
			"$REPO/tools/out/pfctl" "$REPO/tests/confs/"*.conf pf0:/tmp/ 2>/dev/null; then
			break
		fi
		sleep 2
	done
	# stage onto pf0 /opt/pf via mgmt alias already copied to /tmp
	ssh "$DEB" "$SSH 'mkdir -p /opt/pf; mv /tmp/pf.ko /tmp/pfctl /opt/pf/ 2>/dev/null; true'" >/dev/null
	ssh "$DEB" "$SSH 'test -s /opt/pf/pf.ko && test -x /opt/pf/pfctl'" >/dev/null 2>&1 || {
		echo "deploy: pf.ko/pfctl missing on pf0"; return 1; }
}
PUSH=0 FAIL=0
# Never leave pf0 with pf enabled when the runner exits (a failed gate
# would otherwise keep filtering the lab traffic).
pf_disable_pf0() {
	ssh -o ConnectTimeout=8 "$DEB" "$SSH '/opt/pf/pfctl -d >/dev/null 2>&1; true'" \
		>/dev/null 2>&1 || true
}
trap pf_disable_pf0 EXIT INT TERM
ONLY="${1:-}"
for t in "$REPO"/tests/vm/[0-9]*.sh; do
	[ -n "$ONLY" ] && case "$t" in *"$ONLY"*) ;; *) continue ;; esac
	[ -e "$t" ] || continue
	name="$(basename "$t")"
	echo "=== $name ==="
	reset_pf0 || { echo "FAIL $name (vm reset)"; FAIL=$((FAIL+1)); continue; }
	deploy || { echo "FAIL $name (deploy)"; FAIL=$((FAIL+1)); continue; }
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
