#!/usr/bin/env bash
# Build Linux 7.3.4 (VM-lab config) + collect boot artifacts into out/.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
K="${KERNELDIR:-$(cd "$REPO/../linux-7.3.4" && pwd)}"
cd "$K"
"$REPO/scripts/kernel-config.sh" "${1:-}"
JOBS="${JOBS:-$(($(nproc)-2))}"
make -j"$JOBS" -s bzImage modules_prepare
# External module builds need Module.symvers (kbuild only writes
# vmlinux.symvers) and scripts/module.lds (from modules_prepare above).
[ -f Module.symvers ] || cp vmlinux.symvers Module.symvers
[ -f scripts/module.lds ] || { echo "FATAL: scripts/module.lds missing" >&2; exit 1; }
mkdir -p "$REPO/out"
KVER="$(make -s kernelversion)"
cp -f arch/x86/boot/bzImage "$REPO/out/linux-${KVER}-bzImage"
cp -f System.map "$REPO/out/System.map"
cp -f .config "$REPO/out/.config"
make -s kernelrelease > "$REPO/out/kernelversion"
sha256sum "$REPO/out/linux-${KVER}-bzImage" > "$REPO/out/bzImage.sha256"
echo "KERNEL_BUILD_OK $(cat "$REPO/out/kernelversion")"
