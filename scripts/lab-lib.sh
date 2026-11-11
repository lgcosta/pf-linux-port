#!/usr/bin/env bash
# lab-lib.sh — helpers shared by vm-create.sh / vm-destroy.sh / vm-deploy.sh.
# Source it; it never executes anything on its own. Topology single-source:
# lab/ips.env. Disks: per-VM qcow2 with backing_file to the shared read-only
# alpine-base.qcow2 (fast creates, ~MBs of delta storage; the base MUST NOT be
# modified once children exist — vm-create only re-syncs it when the laptop
# source hash differs).
# shellcheck disable=SC2034

LAB_LIB="$(readlink -f "${BASH_SOURCE[0]}")"
: "${REPO:=$(cd "$(dirname "$LAB_LIB")/.." && pwd)}"
export REPO

log()  { printf 'pflab: %s\n' "$*"; }
warn() { printf 'pflab WARN: %s\n' "$*" >&2; }
die()  { printf 'pflab FAIL: %s\n' "$*" >&2; exit 1; }

lab_env() {
  [ -f "$REPO/lab/ips.env" ] || die "missing $REPO/lab/ips.env"
  # shellcheck disable=SC1091
  . "$REPO/lab/ips.env"
  # bare-IP derivatives (route/via fields)
  PF0_LAN_IP=${PF0_LAN%%/*}; PF0_WAN_IP=${PF0_WAN%%/*}; PF0_WAN2_IP=${PF0_WAN2%%/*}
  CLI_IP=${CLI%%/*}; SRV_IP=${SRV%%/*}; GW2_WAN_IP=${GW2_WAN%%/*}; GW2_WAN2_IP=${GW2_WAN2%%/*}
  LAB_SEEDS="$REPO/lab/seeds"
  VMS="pf0 cli srv gw2"
}

SSH_OPTS=(-o BatchMode=yes -o StrictHostKeyChecking=accept-new -o ConnectTimeout=10
          -o LogLevel=ERROR -o ControlMaster=auto -o ControlPersist=120
          -o "ControlPath=$HOME/.ssh/cm-pflab-%r@%h:%p")

vm_name() { echo "pftest-$1"; }
vm_ip()   { local v=${1^^}_SSH; [ -n "${!v-}" ] || die "unknown vm alias: $1"; echo "${!v}"; }

# Run command(s) on deb.
deb()     { ssh "${SSH_OPTS[@]}" "$DEB_USER@$DEB" "$@"; }
deb_cmd() { deb "bash -s" <<<"$1"; }

# Run a shell script inside a lab VM (nested ssh from deb with the lab key).
# Guest host keys change every lab rebuild -> pin-noise disabled for this hop.
VMSSH_OPTS="-o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=5 -o LogLevel=ERROR"
vm_cmd()  { local ip=$1; deb "ssh -i $LAB_KEY_DEB $VMSSH_OPTS root@$ip 'sh -s'" <<<"$2"; }

# --- template rendering: replace @KEY@ tokens from environment + extra K=V ---
RENDER_KEYS=(
  DEB DEB_USER
  PF0_MGMT PF0_LAN PF0_WAN PF0_WAN2 CLI CLI_GW SRV SRV_GW GW2_WAN2 GW2_WAN
  PF0_SSH CLI_SSH SRV_SSH GW2_SSH
  PF0_LAN_IP PF0_WAN_IP PF0_WAN2_IP CLI_IP SRV_IP GW2_WAN_IP GW2_WAN2_IP
  MGMT_NET NET_PF_LAN NET_PF_WAN NET_PF_WAN2
  PF_LAN_HOSTIP PF_WAN_HOSTIP PF_WAN2_HOSTIP PF_NET_PREFIX
  VM_VCPUS VM_MEM_MB
  PF0_MAC_MGMT PF0_MAC_LAN PF0_MAC_WAN PF0_MAC_WAN2
  CLI_MAC_MGMT CLI_MAC_LAN SRV_MAC_MGMT SRV_MAC_WAN GW2_MAC_WAN GW2_MAC_WAN2
  ROOT_PASS LAB_PACKAGES
  IMG_DIR BASE_IMG SEED_DIR DEPLOY_DIR BOOT_DIR
  LAB_KEY_DEB LAB_KEY_LAPTOP LAPTOP_SSH_FRAG BASE_SRC_REL
)
_esc() { local s=$1; s=${s//\\/\\\\}; s=${s//&/\\&}; s=${s//\~/\\~}; s=${s//$'\n'/\\n}; printf '%s' "$s"; }
render() { # render <template> <out> [KEY=VALUE ...]
  local tmpl=$1 out=$2 kv; shift 2
  local -a prog=()
  for kv in "$@"; do
    prog+=(-e "s~@${kv%%=*}@~$(_esc "${kv#*=}")~g")
  done
  for k in "${RENDER_KEYS[@]}"; do
    prog+=(-e "s~@$k@~$(_esc "${!k-}")~g")
  done
  sed "${prog[@]}" "$tmpl" > "$out"
}

# --- deb host prerequisites ---
host_prereqs() {
  deb true || die "cannot ssh $DEB_USER@$DEB (key auth? host up?)"
  deb_cmd "set -e
systemctl is-active --quiet libvirtd || systemctl enable --now libvirtd 2>/dev/null || systemctl enable --now virtqemud
virsh -c qemu:///system list --all >/dev/null
command -v rsync >/dev/null || { apt-get update -qq; DEBIAN_FRONTEND=noninteractive apt-get install -y -qq rsync; }
command -v genisoimage >/dev/null || { DEBIAN_FRONTEND=noninteractive apt-get install -y -qq genisoimage; }
mkdir -p $SEED_DIR $BOOT_DIR"
}

# --- libvirt networks: default (start+autostart) + isolated pf-* L2 nets ---
# pf-* carry a host bridge IP (.254) so deb can ssh/icmp into VM data NICs;
# no <dhcp> and forward=none -> pure isolated segments, guest gw IPs are
# pf0 itself (no host/guest address conflict).
ensure_networks() {
  deb "virsh net-start $MGMT_NET" 2>/dev/null || true
  deb "virsh net-autostart $MGMT_NET" || true
  local entry n b ip
  for entry in "pf-lan $NET_PF_LAN $PF_LAN_HOSTIP" "pf-wan $NET_PF_WAN $PF_WAN_HOSTIP" "pf-wan2 $NET_PF_WAN2 $PF_WAN2_HOSTIP"; do
    set -- $entry; b=$1; n=$2; ip=$3
    if ! deb "virsh net-info $n" >/dev/null 2>&1; then
      printf '<network>\n  <name>%s</name>\n  <forward mode="none"/>\n  <bridge name="%s" stp="on" delay="0"/>\n  <ip address="%s" netmask="%s"/>\n</network>\n' \
        "$n" "virbr-$b" "$ip" "$PF_NET_PREFIX" | deb "virsh net-define /dev/stdin" >/dev/null
    fi
    deb "virsh net-start $n" 2>/dev/null || true
    deb "virsh net-autostart $n" || true
  done
}

# --- lab SSH keypair: private stays on deb; laptop keeps a copy for ProxyJump ---
lab_keypair() {
  deb "test -f $LAB_KEY_DEB" || deb "ssh-keygen -q -t ed25519 -N '' -f $LAB_KEY_DEB -C pflab@deb"
  LAB_PUBKEY=$(deb "cat $LAB_KEY_DEB.pub")
  [ -n "$LAB_PUBKEY" ] || die "could not read lab pubkey on deb"
  if [ ! -f "$LAB_KEY_LAPTOP" ] || ! cmp -s <(deb "cat $LAB_KEY_DEB") "$LAB_KEY_LAPTOP"; then
    umask 077
    deb "cat $LAB_KEY_DEB" > "$LAB_KEY_LAPTOP"
    deb "cat $LAB_KEY_DEB.pub" > "$LAB_KEY_LAPTOP.pub"
  fi
}

# --- shared base image (laptop _vendor -> deb), synced once by sha256 ---
base_image_sync() {
  local lsha rsha=""
  [ -f "$REPO/$BASE_SRC_REL" ] || die "base image missing: $REPO/$BASE_SRC_REL"
  lsha=$(sha256sum "$REPO/$BASE_SRC_REL" | cut -d' ' -f1)
  if [ "${RECREATE_BASE:-0}" != 1 ]; then
    rsha=$(deb "sha256sum $BASE_IMG 2>/dev/null" | cut -d' ' -f1 || true)
  fi
  if [ "$lsha" = "$rsha" ]; then log "base image current on deb"; return 0; fi
  log "syncing base image -> deb:$BASE_IMG"
  rsync -a --info=progress2 -e "ssh ${SSH_OPTS[*]}" "$REPO/$BASE_SRC_REL" "$DEB_USER@$DEB:$BASE_IMG"
}

# --- guest boot kernel: the alpine cloud qcow2 ships an EMPTY ESP + no MBR
# code, so it is not BIOS- nor UEFI-bootable in QEMU as delivered. We extract
# the image's own kernel+initramfs (once, cached on deb) and direct-boot all
# VMs with root=UUID of the base's ext4 partition (the same backing image).
ensure_guest_kernel() {
  deb "test -f $BOOT_DIR/guest-vmlinuz -a -f $BOOT_DIR/guest-initramfs -a -f $BOOT_DIR/guest-root.uuid" \
    && return 0
  log "extracting guest kernel/initramfs from base image"
  deb_cmd "set -e
    modprobe nbd max_part=8 2>/dev/null || true
    qemu-nbd -r -c /dev/nbd1 $BASE_IMG
    trap 'umount /mnt/pflab-extract 2>/dev/null || true; qemu-nbd -d /dev/nbd1' EXIT
    for i in 1 2 3 4 5; do [ -b /dev/nbd1p2 ] && break; sleep 1; done
    [ -b /dev/nbd1p2 ]
    mkdir -p /mnt/pflab-extract $BOOT_DIR
    mount -o ro /dev/nbd1p2 /mnt/pflab-extract
    cp /mnt/pflab-extract/boot/vmlinuz-lts $BOOT_DIR/guest-vmlinuz
    cp /mnt/pflab-extract/boot/initramfs-lts $BOOT_DIR/guest-initramfs
    blkid -s UUID -o value /dev/nbd1p2 > $BOOT_DIR/guest-root.uuid
    chmod a+r $BOOT_DIR/guest-vmlinuz $BOOT_DIR/guest-initramfs $BOOT_DIR/guest-root.uuid"
}

default_kernelblock() { # prints kernelblock xml for extracted direct boot
  local uuid
  uuid=$(deb "cat $BOOT_DIR/guest-root.uuid")
  printf '    <kernel>%s/guest-vmlinuz</kernel>\n    <initrd>%s/guest-initramfs</initrd>\n    <cmdline>root=UUID=%s ro console=ttyS0,115200n8 modules=sd-mod,usb-storage,ext4,ena,gve</cmdline>' \
    "$BOOT_DIR" "$BOOT_DIR" "$uuid"
}

# --- per-role runcmd additions ---
runcmd_extra() {
  case $1 in
    pf0)
      cat <<'EOF'
  - |
    mkdir -p /etc/local.d
    cat > /etc/local.d/pflab.start <<'EOS'
    #!/bin/sh
    sysctl -q -p /etc/sysctl.d/00-pflab.conf 2>/dev/null
    for f in /proc/sys/net/ipv4/conf/*/rp_filter; do echo 0 > "$f" 2>/dev/null; done
    IF=$(ip route show default 2>/dev/null | awk '{print $5; exit}')
    [ -n "$IF" ] || exit 0
    iptables -t nat -A POSTROUTING -o "$IF" -j MASQUERADE
    EOS
    chmod +x /etc/local.d/pflab.start
    rc-update add local boot >/dev/null 2>&1 || true
    /etc/local.d/pflab.start
EOF
      ;;
    cli) echo "  - ip route replace default via $CLI_GW" ;;
    srv) echo "  - ip route replace default via $SRV_GW" ;;
    gw2) echo "  - ip route replace default via $PF0_WAN_IP" ;;
  esac
}

# --- one VM: seed iso + backing disk + domain define/start ---
create_domain() { # create_domain <alias> [kernelblock]
  local a=$1 kblock=${2:-} name disk extra seeds
  name=$(vm_name "$a"); disk="$IMG_DIR/$name.qcow2"
  seeds="$LAB_SEEDS/$name"
  deb_cmd "virsh destroy $name 2>/dev/null; virsh undefine $name --nvram 2>/dev/null; virsh undefine $name 2>/dev/null; rm -f /var/lib/libvirt/qemu/nvram/$name*.raw; true"
  rm -rf "$seeds"; mkdir -p "$seeds"
  extra=$(runcmd_extra "$a")
  render "$REPO/lab/cloud-init/user-data.tmpl" "$seeds/user-data" \
    "VM=$a" "RUNCMD_EXTRA=$extra" "SSH_PUBKEY=$LAB_PUBKEY"
  render "$REPO/lab/cloud-init/meta-data.tmpl" "$seeds/meta-data" "VM=$a"
  render "$REPO/lab/cloud-init/network-config.$a.tmpl" "$seeds/network-config"
  render "$REPO/lab/domains/$name.xml.tmpl" "$seeds/$name.xml" \
    "NAME=$name" "VM=$a" "DISK=$disk" "SEED=$SEED_DIR/$name/seed.iso" "KERNELBLOCK=$kblock"
  rsync -rt --delete --chmod=D0755,F0644 -e "ssh ${SSH_OPTS[*]}" "$seeds/" "$DEB_USER@$DEB:$SEED_DIR/$name/"
  deb_cmd "cd '$SEED_DIR/$name' && rm -f seed.iso && \
    genisoimage -quiet -o seed.iso -volid CIDATA -joliet -rock user-data meta-data network-config"
  deb_cmd "chmod a+rX '$SEED_DIR' '$SEED_DIR/$name'"
  deb_cmd "rm -f '$disk' && qemu-img create -f qcow2 -F qcow2 -b '$BASE_IMG' '$disk' >/dev/null"
  deb "virsh define $SEED_DIR/$name/$name.xml" >/dev/null
  deb "virsh start $name" >/dev/null
  log "started $name"
}

wait_ready() { # wait_ready <alias> [timeout-s] — polls cloud-init done marker
  local a=$1 t=${2:-300} ip name el=0
  ip=$(vm_ip "$a"); name=$(vm_name "$a")
  while :; do
    if vm_cmd "$ip" 'test -f /run/pf-lab-ready' 2>/dev/null; then
      log "$a ready"; return 0
    fi
    deb "LC_ALL=C virsh dominfo $name 2>/dev/null | grep -q running" \
      || die "$name is not running (virsh console $name)"
    sleep 5; el=$((el+5))
    [ "$el" -ge "$t" ] && die "timeout waiting for $a cloud-init"
  done
}

packages_fixup() { # safety net if cloud-init ran before pf0 had transit up
  local a ip
  for a in $VMS; do
    ip=$(vm_ip "$a")
    vm_cmd "$ip" 'command -v iperf3 >/dev/null 2>&1 && command -v iptables >/dev/null 2>&1 || {
      grep -q nameserver /etc/resolv.conf 2>/dev/null || echo nameserver 192.168.122.1 >> /etc/resolv.conf
      . /etc/os-release
      grep -qE "^[^#]*/community" /etc/apk/repositories || echo "${HOME}/v${VERSION_ID}/community" >> /etc/apk/repositories
      apk update -q >/dev/null 2>&1
      apk add -q iperf3 tcpdump python3 scapy bash iptables >/dev/null 2>&1 || warn "apk add failed on '"$a"'"
    }' || warn "no ssh to $a for package fixup"
  done
}

# --- ssh access plumbing: aliases pf0|cli|srv|gw2 from deb and from laptop ---
laptop_ssh_config() {
  # fresh pin file per lab rebuild: guest host keys change with recreated disks
  rm -f "$HOME/.ssh/known_hosts_pflab"
  mkdir -p "$(dirname "$LAPTOP_SSH_FRAG")"; chmod 700 "$(dirname "$LAPTOP_SSH_FRAG")" 2>/dev/null || true
  cat > "$LAPTOP_SSH_FRAG" <<EOF
# >>> generated by pf_port/scripts/vm-create.sh (topology: lab/ips.env) >>>
Host pf0 cli srv gw2
  User root
  IdentityFile $LAB_KEY_LAPTOP
  ProxyJump $DEB_USER@$DEB
  StrictHostKeyChecking accept-new
  UserKnownHostsFile $HOME/.ssh/known_hosts_pflab
  ConnectTimeout 15
  ServerAliveInterval 15
Host pf0
  HostName $PF0_SSH
Host cli
  HostName $CLI_SSH
Host srv
  HostName $SRV_SSH
Host gw2
  HostName $GW2_SSH
# <<< generated <<<
EOF
  touch ~/.ssh/config
  grep -qE '^Include[[:space:]]+~/.ssh/config.d' ~/.ssh/config || {
    cp -p ~/.ssh/config ~/.ssh/config.pflab-bak 2>/dev/null || true
    printf 'Include ~/.ssh/config.d/*\n' | cat - ~/.ssh/config > ~/.ssh/config.tmp && mv ~/.ssh/config.tmp ~/.ssh/config
    log "added Include ~/.ssh/config.d/* to ~/.ssh/config (backup: ~/.ssh/config.pflab-bak)"
  }
}

deb_ssh_config() {
  deb_cmd 'f=$HOME/.ssh/config; touch "$f"
    sed -i "/# >>> pflab/,/# <<< pflab/d" "$f"
    cat >> "$f" <<EOF
# >>> pflab (generated by vm-create.sh) >>>
Host pf0 cli srv gw2
  User root
  IdentityFile '"$LAB_KEY_DEB"'
  StrictHostKeyChecking no
  UserKnownHostsFile /dev/null
  LogLevel ERROR
  ConnectTimeout 8
Host pf0
  HostName '"$PF0_SSH"'
Host cli
  HostName '"$CLI_SSH"'
Host srv
  HostName '"$SRV_SSH"'
Host gw2
  HostName '"$GW2_SSH"'
# <<< pflab <<<
EOF'
}
