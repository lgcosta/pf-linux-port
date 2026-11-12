#!/usr/bin/env bash
# prepare-host.sh — turn a fresh Debian 13 (trixie) box into a PF-on-Linux
# build host + libvirt lab host.
#
# Idempotent: safe to re-run.  It only installs packages and reports what the
# toolchain looks like; it never builds anything.
#
#   sudo ./scripts/debian13/prepare-host.sh            # build + lab host
#   sudo ./scripts/debian13/prepare-host.sh --lab-only # libvirt side only
#   sudo ./scripts/debian13/prepare-host.sh --build-only
set -euo pipefail

MODE=both
case "${1:-}" in
--lab-only)   MODE=lab ;;
--build-only) MODE=build ;;
--help|-h)    sed -n '2,10p' "$0"; exit 0 ;;
*)            [ $# -eq 0 ] || { echo "unknown option: $1" >&2; exit 2; } ;;
esac

export DEBIAN_FRONTEND=noninteractive

BUILD_PKGS="build-essential bc bison flex libssl-dev libelf-dev rsync curl ca-certificates"
LAB_PKGS="libvirt-daemon-system libvirt-clients qemu-system-x86 qemu-utils genisoimage bridge-utils netcat-openbsd iperf3 tcpdump python3-scapy"

if [ "$(id -u)" -ne 0 ]; then
	echo "this script needs root (apt install)" >&2
	exit 1
fi

install() { # install <list>
	echo "== apt-get update"
	apt-get update -qq
	echo "== apt-get install: $*"
	apt-get install -y -qq $*
}

case "$MODE" in
build) install "$BUILD_PKGS" ;;
lab)   install "$LAB_PKGS" ;;
both)  install "$BUILD_PKGS $LAB_PKGS" ;;
esac

echo
echo "== toolchain"
printf '%-12s %s\n' gcc      "$(gcc -dumpversion 2>/dev/null || echo MISSING)"
printf '%-12s %s\n' make     "$(make --version 2>/dev/null | sed -n 1p || echo MISSING)"
printf '%-12s %s\n' bison    "$(bison --version 2>/dev/null | sed -n 1p || echo MISSING)"
printf '%-12s %s\n' flex     "$(flex --version 2>/dev/null || echo MISSING)"
printf '%-12s %s\n' bc       "$(bc --version 2>/dev/null | sed -n 1p || echo MISSING)"
printf '%-12s %s\n' rsync    "$(rsync --version 2>/dev/null | sed -n 1p || echo MISSING)"
printf '%-12s %s\n' libvirt  "$(virsh --version 2>/dev/null || echo 'not installed')"
printf '%-12s %s\n' qemu     "$(qemu-system-x86_64 --version 2>/dev/null | sed -n 1p || echo 'not installed')"
printf '%-12s %s\n' geniso   "$(genisoimage --version 2>&1 | sed -n 1p || echo 'not installed')"

echo
echo "== notes"
echo "- Debian 13 ships gcc 14 as the default compiler; gcc-15 is not in"
echo "  trixie.  The kernel tree and pf.ko build with gcc 14 (see"
echo "  docs/pt-BR/debian13.md for the verified run)."
echo "- pf.ko is built against the Linux 7.3.4 tree, NOT against the Debian"
echo "  kernel: it only loads on a 7.3.4 kernel (vermagic).  Build the"
echo "  kernel and the module from the same tree."
echo
echo "PREPARE_HOST_OK"
