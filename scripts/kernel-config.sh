#!/usr/bin/env bash
# Configure the Linux 7.3.4 tree for PF-on-Linux VM lab use:
# defconfig baseline + direct-kernel-boot deps. Idempotent.
# `kernel-config.sh test` additionally enables DEBUG_KMEMLEAK (soak builds).
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
# Default: the kernel tree next to the repository (pf_port/../linux-7.3.4),
# same rule as build-kernel.sh / build-module.sh.
K="${KERNELDIR:-$REPO/../linux-7.3.4}"
cd "$K"
[ -f .config ] || make defconfig
scripts/config --file .config \
  --set-val CONFIG_VIRTIO_BLK y \
  --set-val CONFIG_VIRTIO_PCI y \
  --set-val CONFIG_VIRTIO_NET y \
  --set-val CONFIG_EXT4_FS y \
  --set-val CONFIG_NETFILTER y \
  --enable  CONFIG_NETFILTER_ADVANCED \
  --set-val CONFIG_NET_SCHED y \
  --set-val CONFIG_INET y \
  --set-val CONFIG_IPV6 y \
  --set-val CONFIG_SERIAL_8250 y \
  --enable  CONFIG_SERIAL_8250_CONSOLE
if [ "${1:-}" = "test" ]; then
  scripts/config --file .config --set-val CONFIG_DEBUG_INFO y --enable CONFIG_DEBUG_KMEMLEAK
fi
make olddefconfig
echo "--- effective ---"
grep -E '^(CONFIG_VIRTIO_(BLK|PCI|NET)|CONFIG_EXT4_FS|CONFIG_NETFILTER|CONFIG_NET_SCHED|CONFIG_INET|CONFIG_IPV6|CONFIG_DEBUG_KMEMLEAK)(=| is not)' .config || true
