#!/usr/bin/env bash
# vm-destroy.sh — tear the lab down: pftest-* domains, their qcow2 disks,
# seed ISOs/dirs, the pf-lan/pf-wan/pf-wan2 networks, and the generated
# laptop ssh fragment. The shared alpine-base.qcow2 and 'default' network are
# KEPT by default (re-create is then fast); --purge-base removes the base,
# --purge-keys also drops the lab keypair on deb + laptop copies.
# Never touches non-pftest domains or the 'default' network state.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck disable=SC1091
. "$REPO/scripts/lab-lib.sh"

PURGE_BASE=0; PURGE_KEYS=0; QUIET=0
for arg in "$@"; do
  case $arg in
    --purge-base) PURGE_BASE=1 ;;
    --purge-keys) PURGE_KEYS=1 ;;
    --quiet) QUIET=1 ;;
    -h|--help) sed -n '2,8p' "$0"; exit 0 ;;
    *) die "unknown arg: $arg (-h for usage)" ;;
  esac
done
[ $QUIET = 1 ] && exec >/dev/null 2>&1

lab_env
deb true || { warn "deb unreachable; removing laptop artifacts only"; rm -f "$LAPTOP_SSH_FRAG"; exit 0; }

for a in $VMS; do
  name=$(vm_name "$a")
  deb_cmd "virsh destroy $name 2>/dev/null; virsh undefine $name --nvram 2>/dev/null; virsh undefine $name 2>/dev/null; rm -f /var/lib/libvirt/qemu/nvram/$name*.raw; true"
  deb_cmd "rm -rf '$SEED_DIR/$name' '$IMG_DIR/$name.qcow2' /tmp/$name.xml"
  log "removed domain+disk+seed: $name"
done
rm -rf "$LAB_SEEDS"

for n in "$NET_PF_LAN" "$NET_PF_WAN" "$NET_PF_WAN2"; do
  deb_cmd "virsh net-destroy $n 2>/dev/null; virsh net-undefine $n 2>/dev/null; true"
done
log "removed networks: $NET_PF_LAN $NET_PF_WAN $NET_PF_WAN2 (default left as-is)"

deb_cmd 'f=$HOME/.ssh/config; [ -f "$f" ] && sed -i "/# >>> pflab/,/# <<< pflab/d" "$f" || true'
rm -f "$LAPTOP_SSH_FRAG" "$HOME/.ssh/known_hosts_pflab"
log "removed generated ssh fragments (laptop + deb config blocks)"

if [ $PURGE_BASE = 1 ]; then
  deb "rm -f $BASE_IMG"
  log "removed base image on deb"
fi
if [ $PURGE_KEYS = 1 ]; then
  deb "rm -f $LAB_KEY_DEB $LAB_KEY_DEB.pub"
  rm -f "$LAB_KEY_LAPTOP" "$LAB_KEY_LAPTOP.pub"
  log "removed lab keypair (deb + laptop)"
fi

log "VERIFY state on deb:"
deb 'virsh list --all; echo ---; virsh net-list --all; echo ---; ls -l /var/lib/libvirt/images/ /var/lib/libvirt/pflab/ 2>/dev/null || true'
