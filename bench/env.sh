#!/usr/bin/env bash
# bench/env.sh — shared environment for the PF vs nftables/iptables benchmark.
# Sourced by every scenario script; never executes on its own.
set -uo pipefail

BENCH_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$BENCH_ROOT/.." && pwd)"
OUTD="${BENCH_OUT:-$BENCH_ROOT/data}"
mkdir -p "$OUTD"
RUN_STAMP="$(date +%Y%m%d-%H%M%S)"
RUN_DIR="$OUTD/run-$RUN_STAMP"
mkdir -p "$RUN_DIR"

DEB="${DEB:-root@192.168.1.242}"
FW="${FW:-root@10.0.0.1}"        # the firewall VM's management address
KEDGE="${KEDGE:-root@10.0.0.2}"  # the traffic source VM (client)
SRV="${SRV:-root@10.0.0.3}"      # the traffic sink VM (server)
VBASE=(ssh -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=8)
SSH_FW="${VBASE[*]} $FW"
SSH_KEDGE="${VBASE[*]} $KEDGE"
SSH_SRV="${VBASE[*]} $SRV"

# A single run of every comparable measurement, on one backend.
# The caller sets BACKEND, RULES and optionally BENCH_SECONDS / BENCH_STREAMS.

log()  { printf 'bench: %s\n' "$*" >&2; }
die()  { printf 'bench FAIL: %s\n' "$*" >&2; exit 1; }

# One SSH convenience: run a command on a VM, capture stdout+stderr to a file
# and echo it; every capture is kept in RUN_DIR for traceability.
on_fw()    { $SSH_FW "$@"; }
on_kedge() { $SSH_KEDGE "$*"; }
on_srv()   { $SSH_SRV "$*"; }

capture() { # capture <name> <vm> <command...>
	local name="$1" vm="$2"; shift 2
	case "$vm" in
		fw)    $SSH_FW "$@" ;;
		kedge) $SSH_KEDGE "$@" ;;
		srv)   $SSH_SRV "$@" ;;
		*) die "capture: unknown vm $vm" ;;
	esac
}
