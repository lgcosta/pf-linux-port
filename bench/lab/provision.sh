#!/usr/bin/env bash
# bench/lab/provision.sh — build the benchmark lab on the hypervisor.
#
# Creates three VMs on an isolated bridge (bench-net):
#   bench-fw    the firewall under test (runs pf.ko and the native backends)
#   bench-kedge traffic source (10.0.0.2)
#   bench-srv   traffic sink   (10.0.1.2)
# with the firewall in the middle (10.0.0.1 / 10.0.1.1, ip_forward=1).
#
# The firewall boots the self-built 7.3.4 kernel (bench-bzImage) so that the
# PF module and the native backends run on the *same* kernel.
#
# Idempotent: re-running recreates the VMs and re-applies everything.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

DEB="${DEB:-root@192.168.1.242}"
REPO="$(cd ../.. && pwd)"
BZIMAGE="${BZIMAGE:-$REPO/out/linux-7.3.4-bzImage}"
MODULE="${MODULE:-$REPO/module/pf.ko}"
PFCTL="${PFCTL:-$REPO/tools/out/pfctl}"
KEY="${LAB_KEY_DEB:-/root/.ssh/pflab_ed}"

say() { printf 'bench-provision: %s\n' "$*" >&2; }

say "pushing lab definitions to $DEB"
ssh -o BatchMode=yes "$DEB" 'mkdir -p /root/bench/lab'
rsync -a -e "ssh -o BatchMode=yes" ./*.xml ./*.network ./*.user "$DEB":/root/bench/lab/

say "regenerating the cloud-init seeds (public key injected)"
PUB="$(ssh -o BatchMode=yes "$DEB" "cat $KEY.pub")"
for v in fw kedge srv; do
	sed "s|      - PLACEHOLDER|      - $PUB|" "$v.user" > "/tmp/bench-$v.user"
	scp -q -o BatchMode=yes "/tmp/bench-$v.user" "$DEB":/var/lib/libvirt/pflab/seeds/bench-$v/user-data
	scp -q -o BatchMode=yes "$v.network" "$DEB":/var/lib/libvirt/pflab/seeds/bench-$v/network-config
	ssh -o BatchMode=yes "$DEB" "printf 'instance-id: bench-$v\nlocal-hostname: $v\n' > /var/lib/libvirt/pflab/seeds/bench-$v/meta-data;
		cd /var/lib/libvirt/pflab/seeds/bench-$v && rm -f seed.iso &&
		genisoimage -quiet -o seed.iso -volid CIDATA -joliet -rock user-data meta-data network-config &&
		chmod a+rX /var/lib/libvirt/pflab/seeds/bench-$v"
done

say "uploading the kernel, the module and the netfilter modules"
ssh -o BatchMode=yes "$DEB" 'mkdir -p /var/lib/pflab/boot /var/lib/pflab/modules-net'
scp -q -o BatchMode=yes "$BZIMAGE" "$DEB":/var/lib/pflab/boot/bench-bzImage
scp -q -o BatchMode=yes "$MODULE" "$DEB":/root/pf_port/module/pf.ko
scp -q -o BatchMode=yes "$PFCTL" "$DEB":/root/pf_port/tools/out/pfctl

say "creating the bridge and the VMs"
ssh -o BatchMode=yes "$DEB" 'set -e
	virsh net-define /root/bench/lab/networks.xml 2>/dev/null || true
	virsh net-start bench-net 2>/dev/null || true
	virsh net-autostart bench-net 2>/dev/null || true
	for n in bench-fw bench-kedge bench-srv; do
		virsh destroy $n 2>/dev/null || true
		virsh undefine $n --nvram 2>/dev/null || true
		rm -f /var/lib/libvirt/images/$n.qcow2
		qemu-img create -f qcow2 -F qcow2 \
		    -b /var/lib/libvirt/images/alpine-base.qcow2 \
		    /var/lib/libvirt/images/$n.qcow2 >/dev/null
		virsh define /root/bench/lab/${n#bench-}.xml >/dev/null
		virsh start $n >/dev/null
	done
	echo VMs_STARTED'

say "waiting for the guests (management address + cloud-init)"
ssh -o BatchMode=yes "$DEB" 'for i in $(seq 1 40); do sleep 10; ok=1
	for v in bench-fw bench-kedge bench-srv; do
		ip=$(virsh domifaddr $v 2>/dev/null | awk "/ipv4/ {print \$4}" | cut -d/ -f1)
		{ [ -n "$ip" ] && ssh -i '"$KEY"' -o StrictHostKeyChecking=no \
			-o UserKnownHostsFile=/dev/null -o ConnectTimeout=6 \
			root@$ip true 2>/dev/null; } || ok=0
	done
	[ "$ok" = 1 ] && { echo ALL_UP; break; }
done'

say "installing the backends, the modules and PF inside bench-fw"
ssh -o BatchMode=yes "$DEB" "bash -s" <<EOF
set -e
K=$KEY
V="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=8"
FW=root@192.168.122.203
scp -q -i \$K \$V -r /var/lib/pflab/modules-net/net \$FW:/tmp/ 2>/dev/null || true
ssh -i \$K \$V \$FW 'mkdir -p /lib/modules/7.3.4 && cp -r /tmp/net /lib/modules/7.3.4/ 2>/dev/null; depmod -a 2>/dev/null; true'
ssh -i \$K \$V \$FW 'apk add -q nftables iptables iptables-legacy iperf3 >/dev/null 2>&1; true'
ssh -i \$K \$V \$FW 'mkdir -p /opt/pf'
scp -q -i \$K \$V /root/pf_port/module/pf.ko \$FW:/opt/pf/pf.ko
scp -q -i \$K \$V /root/pf_port/tools/out/pfctl \$FW:/opt/pf/pfctl
ssh -i \$K \$V \$FW 'chmod +x /opt/pf/pfctl; insmod /opt/pf/pf.ko 2>/dev/null || true; /opt/pf/pfctl -e 2>/dev/null || true; ls -l /dev/pf'
EOF

say "installing iperf3 on the traffic endpoints"
ssh -o BatchMode=yes "$DEB" 'K='"$KEY"'; V="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=8"
	for ip in 192.168.122.201 192.168.122.202; do
		ssh -i $K $V root@$ip "apk add -q iperf3 >/dev/null 2>&1; true" 2>/dev/null || true
	done'

say "provisioned"
