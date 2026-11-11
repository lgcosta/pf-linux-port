#!/usr/bin/env bash
# All VMs reachable from the libvirt host; pf0 via mgmt DHCP IP (pinned .8)
DEB="${DEB:-root@192.168.1.242}"
PF0_IP=${PF0_IP:-192.168.122.8}
CLI_IP=${CLI_IP:-10.0.1.10}; SRV_IP=${SRV_IP:-10.0.2.20}; GW2_IP=${GW2_IP:-10.0.3.2}
SSHBASE="ssh -o StrictHostKeyChecking=accept-new -o ConnectTimeout=8 -i /root/.ssh/pflab_ed root@"
pf0() { ssh -o ConnectTimeout=10 "$DEB" "$SSHBASE$PF0_IP \"$*\""; }
cli() { ssh -o ConnectTimeout=10 "$DEB" "$SSHBASE$CLI_IP \"$*\""; }
srv() { ssh -o ConnectTimeout=10 "$DEB" "$SSHBASE$SRV_IP \"$*\""; }
gw2() { ssh -o ConnectTimeout=10 "$DEB" "$SSHBASE$GW2_IP \"$*\""; }
# Upload a config to pf0.  The scp through the libvirt jump host is
# occasionally refused ("stdio forwarding failed"); a silent failure used to
# leave a *stale* config on pf0 (or none at all), which showed up as a
# bogus gate failure.  Retry and fail loudly instead.
pfput() {
	local f="$1" i
	for i in 1 2 3; do
		if scp -q -o ProxyJump=root@192.168.1.242 -o StrictHostKeyChecking=accept-new \
			-i "$HOME/.ssh/pflab_ed" "$f" pf0:/tmp/ 2>/dev/null; then
			echo "/tmp/$(basename "$f")"
			return 0
		fi
		sleep 2
	done
	echo "pfput: upload of $f to pf0 failed" >&2
	return 1
}
pfload() { local r; r=$(pfput "$1") || return 1; pf0 "/opt/pf/pfctl -f $r" && return 0; return 1; }
ok()   { echo "OK $*"; }
bad()  { echo "NOT OK: $*" >&2; FAILED=1; }
finish(){ [ "${FAILED:-0}" = 1 ] && exit 1; exit 0; }
