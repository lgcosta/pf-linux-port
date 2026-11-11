#!/usr/bin/env bash
# VM lab test runner. Executes tests/vm/*.sh against the pftest-* lab via
# nested ssh (deb host → VM). Collects per-test PASS/FAIL + logs into
# out/test-logs/. Exit 0 only if every gate test passes.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
LOGD="$REPO/out/test-logs"; mkdir -p "$LOGD"
DEB="${DEB:-root@192.168.1.242}"
source "$REPO/lab/ips.env" 2>/dev/null || true

vm() { # vm <alias> <cmd...>
	local alias="$1"; shift
	ssh -o ConnectTimeout=10 "$DEB" "ssh -o StrictHostKeyChecking=accept-new -o ConnectTimeout=8 -i /root/.ssh/pflab_ed root@${!alias:-$(grep -h "^${alias^^}=" "$REPO/lab/ips.env" 2>/dev/null | cut -d= -f2)} \"$*\""
}

PUSH=0 FAIL=0 SKIP=0
for t in "$REPO"/tests/vm/[0-9]*.sh; do
	[ -e "$t" ] || continue
	name="$(basename "$t")"
	echo "=== $name ==="
	if bash "$t" >"$LOGD/$name.log" 2>&1; then
		echo "PASS $name"; PUSH=$((PUSH+1))
	else
		rc=$?
		if [ $rc -eq 77 ]; then echo "SKIP $name"; SKIP=$((SKIP+1));
		else echo "FAIL $name (rc=$rc, log $LOGD/$name.log)"; FAIL=$((FAIL+1)); fi
	fi
done
echo "----------------------------------------"
echo "PASS=$PUSH FAIL=$FAIL SKIP=$SKIP"
[ $FAIL -eq 0 ]
