#!/usr/bin/env bash
# Shared helpers for the screencast phase scripts.
#
# Everything printed by a phase is the REAL output of the lab: Alpine VMs
# under libvirt running the kernel built from source with pf.ko.  Only the
# framing (prompts, banners) is scripted.
set -u

DEB=${DEB:-root@192.168.1.242}
KEY=${KEY:-$HOME/.ssh/pflab_ed}
CLI_IP=${CLI_IP:-10.0.1.10}
SRV_IP=${SRV_IP:-10.0.2.20}
GW2_IP=${GW2_IP:-10.0.3.2}
REPO=${REPO:-$(cd "$(dirname "$0")/../../.." && pwd)}

cyan()  { printf '\033[1;36m%s\033[0m\n' "$*"; }
green() { printf '\033[1;32m%s\033[0m\n' "$*"; }
yellow(){ printf '\033[1;33m%s\033[0m\n' "$*"; }
bold()  { printf '\033[1m%s\033[0m\n' "$*"; }
dim()   { printf '\033[2m%s\033[0m\n' "$*"; }

banner() {
	echo
	bold "==============================================================="
	bold " $*"
	bold "==============================================================="
	echo
}

mgmt() {
	ssh -o ConnectTimeout=8 "$DEB" "virsh domifaddr $1 | awk '/vnet/{print \$4}' | cut -d/ -f1" \
	    2>/dev/null | tr -d '\r' | head -1
}

PF0_MGMT=${PF0_MGMT:-$(mgmt pftest-pf0)}
CLI_MGMT=${CLI_MGMT:-$(mgmt pftest-cli)}
SRV_MGMT=${SRV_MGMT:-$(mgmt pftest-srv)}
[ -n "$PF0_MGMT" ] || PF0_MGMT=192.168.122.8
[ -n "$CLI_MGMT" ] || CLI_MGMT=192.168.122.230
[ -n "$SRV_MGMT" ] || SRV_MGMT=192.168.122.202

onvm() {
	local ip=$1; shift
	ssh -o ConnectTimeout=10 -o StrictHostKeyChecking=accept-new -o LogLevel=ERROR \
	    -o ProxyJump=root@192.168.1.242 -i "$KEY" root@"$ip" "$@" 2>&1
}

# run on the firewall VM, showing the command as a prompt
p0() {
	cyan "pf0# $*"
	onvm "$PF0_MGMT" "$@"
	sleep 0.7
}

# run on the client VM
vmc() {
	cyan "cli# $*"
	onvm "$CLI_MGMT" "$@"
	sleep 0.7
}

# run on the server VM
vms() {
	cyan "srv# $*"
	onvm "$SRV_MGMT" "$@"
	sleep 0.7
}

# upload a local file to the firewall VM
put() {
	scp -q -o ProxyJump=root@192.168.1.242 -i "$KEY" "$1" "root@$PF0_MGMT:/tmp/" 2>/dev/null
}
