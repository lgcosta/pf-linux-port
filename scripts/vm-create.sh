#!/usr/bin/env bash
# vm-create.sh — build (or rebuild) the pftest-* libvirt lab on deb.
# Topology + paths: lab/ips.env. Idempotent: pre-existing pftest-* domains,
# disks and seeds are torn down and recreated; shared base image and data
# networks are reused when already correct. Leave all VMs running at exit.
#
#   --pf0-kernel=PATH   direct kernel boot for pf0 (laptop path is rsynced to
#                       deb:BOOT_DIR/bzImage; a deb-local path is used as-is).
#                       cmdline: root=/dev/vda2 ro console=ttyS0
#   --pf0-initrd=PATH   optional initrd alongside --pf0-kernel
#   --recreate-base     force re-sync of alpine-base.qcow2 from _vendor
#   -h                  help
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck disable=SC1091
. "$REPO/scripts/lab-lib.sh"

PF0_KERNEL=""; PF0_INITRD=""; RECREATE_BASE=0
for arg in "$@"; do
  case $arg in
    --pf0-kernel=*)  PF0_KERNEL=${arg#*=}; [ -n "$PF0_KERNEL" ] || die "--pf0-kernel needs a value" ;;
    --pf0-initrd=*)  PF0_INITRD=${arg#*=} ;;
    --recreate-base) RECREATE_BASE=1 ;;
    -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
    *) die "unknown arg: $arg (-h for usage)" ;;
  esac
done

lab_env
command -v rsync >/dev/null || die "rsync missing on laptop"
command -v genisoimage >/dev/null 2>&1 || true  # iso is built on deb, not here

host_prereqs
ensure_networks
lab_keypair
base_image_sync

# direct kernel boot: every VM boots via the image's own extracted kernel
# (base image is not BIOS/UEFI bootable, see lab-lib.sh); --pf0-kernel
# replaces pf0's kernel with a built bzImage (T6), keeping its root on vda2.
ensure_guest_kernel
DEFAULT_KB=$(default_kernelblock)
PF0_KB="$DEFAULT_KB"
if [ -n "$PF0_KERNEL" ]; then
  deb "mkdir -p $BOOT_DIR"
  KPATH="$PF0_KERNEL"
  if [ -f "$PF0_KERNEL" ]; then
    rsync -a -e "ssh ${SSH_OPTS[*]}" "$PF0_KERNEL" "$DEB_USER@$DEB:$BOOT_DIR/bzImage"
    KPATH="$BOOT_DIR/bzImage"
  fi
  PF0_KB="    <kernel>$KPATH</kernel>"$'\n'"    <cmdline>root=/dev/vda2 ro console=ttyS0,115200n8</cmdline>"
  if [ -n "$PF0_INITRD" ]; then
    IPATH="$PF0_INITRD"
    if [ -f "$PF0_INITRD" ]; then
      rsync -a -e "ssh ${SSH_OPTS[*]}" "$PF0_INITRD" "$DEB_USER@$DEB:$BOOT_DIR/initrd"
      IPATH="$BOOT_DIR/initrd"
    fi
    PF0_KB="    <kernel>$KPATH</kernel>"$'\n'"    <initrd>$IPATH</initrd>"$'\n'"    <cmdline>root=/dev/vda2 ro console=ttyS0,115200n8</cmdline>"
  fi
  log "pf0 direct kernel boot override: $KPATH"
fi

# pf0 first: cli/srv/gw2 cloud-init needs pf0's transit+masq for pkgs
create_domain pf0 "$PF0_KB"
wait_ready pf0 420

for a in cli srv gw2; do create_domain "$a" "$DEFAULT_KB"; done
for a in cli srv gw2; do wait_ready "$a"; done

packages_fixup
deb_ssh_config
laptop_ssh_config

log "validate: ssh pf0 'ip -br a'"
ssh "${SSH_OPTS[@]}" pf0 'ip -br a' || warn "ssh pf0 failed — check $LAPTOP_SSH_FRAG"
log "LAB UP: aliases on laptop: ssh pf0|cli|srv|gw2  (ProxyJump $DEB_USER@$DEB)"
deb "virsh list" || true
