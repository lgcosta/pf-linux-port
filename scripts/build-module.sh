#!/usr/bin/env bash
# Build pf.ko out-of-tree against the patched kernel tree.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
K="${KERNELDIR:-$(cd "$REPO/../linux-7.3.4" && pwd)}"
[ -e "$K/Module.symvers" ] || {
  if [ -f "$K/vmlinux.symvers" ]; then cp "$K/vmlinux.symvers" "$K/Module.symvers";
  else echo "kernel tree not prepared — run scripts/build-kernel.sh first" >&2; exit 1; fi; }
[ -f "$K/scripts/module.lds" ] || make -C "$K" -j4 modules_prepare >/dev/null
make -C "$K" M="$REPO/module" "$@"
echo MODULE_BUILD_OK
