#!/usr/bin/env bash
# bench/lab/seed.sh — generate a cloud-init seed for one bench VM and copy it
# to the hypervisor.  Usage: seed.sh <fw|kedge|srv>
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
V="$1"
[ -n "$V" ] && [ -f "$V.network" ] || { echo "usage: seed.sh <fw|kedge|srv>" >&2; exit 1; }
D="/var/lib/libvirt/pflab/seeds/bench-$V"
ssh -o BatchMode=yes root@192.168.1.242 "mkdir -p $D"
scp -q -o BatchMode=yes "$V.user" "root@192.168.1.242:$D/user-data"
scp -q -o BatchMode=yes "$V.network" "root@192.168.1.242:$D/network-config"
ssh -o BatchMode=yes root@192.168.1.242 "printf 'instance-id: bench-$V\nlocal-hostname: $V\n' > $D/meta-data;
  cd $D && rm -f seed.iso && genisoimage -quiet -o seed.iso -volid CIDATA -joliet -rock user-data meta-data network-config && chmod a+rX $D"
echo "seed for $V written to $D"
