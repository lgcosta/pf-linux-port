#!/usr/bin/env bash
# Demo session for PF-on-Linux: a scripted terminal tour of the lab.
# Recorded with:  asciinema rec out/demo-pf-linux.cast -c "bash scripts/demo-session.sh"
#
# Everything printed is the REAL output of the lab (Alpine VMs under libvirt
# running the kernel built from source with our pf.ko).  The ssh aliases
# (pf0/cli/srv) come from ~/.ssh/config.d/pflab.
set -u

CLI_IP=${CLI_IP:-10.0.1.10}
SRV_IP=${SRV_IP:-10.0.2.20}

bold()  { printf '\033[1m%s\033[0m\n' "$*"; }
cyan()  { printf '\033[1;36m%s\033[0m\n' "$*"; }

p0() {
	cyan "pftest-pf0# $*"
	ssh pf0 "$*" 2>&1 | sed 's/^/  /'
	sleep 0.4
}
vmc() {
	cyan "pftest-cli# $*"
	ssh cli "$*" 2>&1 | sed 's/^/  /'
	sleep 0.4
}

CONF=/tmp/demo-pf.conf
DIV=/tmp/demo-divert.conf

cat > "$CONF" <<'CONF'
# PF-on-Linux demo ruleset (pf.conf syntax, real OpenBSD pfctl)
set block-policy return
table <trusted> { 10.0.1.0/24 }

# transit traffic: stateful, table-driven
pass in quick on ingress proto icmp from <trusted> keep state
pass out quick on egress proto icmp keep state

# deny a specific destination
block drop in quick proto icmp to 10.0.9.99

# everything else
pass in quick
pass out quick
CONF

cat > "$DIV" <<CONF
pass in quick proto icmp from $CLI_IP to 10.0.1.1 divert-to "10.0.1.1" port 10000 keep state
pass out quick
CONF

EGRESS_IF=$(ssh pf0 "ip route get $SRV_IP | grep -o 'dev [a-z0-9]*' | cut -d' ' -f2" 2>/dev/null | tr -d '\r')
[ -n "$EGRESS_IF" ] || EGRESS_IF=eth2

scp -q "$CONF" "$DIV" pf0:/tmp/ 2>/dev/null
scp -q tools/out/pdivert pf0:/tmp/pdivert 2>/dev/null

bold "==============================================================="
bold " PF on Linux - the OpenBSD PF 8.0 engine inside the Linux kernel"
bold "==============================================================="
echo
echo "Lab: 3 Alpine VMs under libvirt."
echo "  pftest-pf0 = firewall (kernel 7.3.4 built from source + pf.ko)"
echo "  pftest-cli = client 10.0.1.10     pftest-srv = server 10.0.2.20"
echo

bold "--- 1. our kernel, the OpenBSD engine as a module ---"
p0 "uname -r"
p0 "lsmod | grep '^pf ' || echo 'pf: not loaded yet'"
p0 "insmod /opt/pf/pf.ko && dmesg | tail -3"
echo

bold "--- 2. pfctl talks to the module over the OpenBSD ioctl ABI ---"
p0 "/opt/pf/pfctl -s info | head -4"
p0 "/opt/pf/pfctl -e && /opt/pf/pfctl -s info | head -1"
echo

bold "--- 3. a real pf.conf: tables, stateful rules, an explicit drop ---"
p0 "cat /tmp/demo-pf.conf"
p0 "/opt/pf/pfctl -f /tmp/demo-pf.conf && echo 'rules loaded'"
p0 "/opt/pf/pfctl -sr"
echo

bold "--- 4. transit traffic is allowed and stateful ---"
vmc "ping -c2 -W2 $SRV_IP | tail -3"
p0 "/opt/pf/pfctl -ss"
echo

bold "--- 5. tables are live objects ---"
p0 "/opt/pf/pfctl -t trusted -T show"
p0 "/opt/pf/pfctl -s Tables"
echo

bold "--- 6. the drop rule actually drops ---"
p0 "cat > /tmp/demo-block.conf <<EOF
block drop in quick proto icmp to $SRV_IP
pass in quick
pass out quick
EOF"
p0 "/opt/pf/pfctl -f /tmp/demo-block.conf && echo 'block rules loaded'"
vmc "ping -c2 -W2 $SRV_IP | tail -3"
p0 "/opt/pf/pfctl -sr -vv | head -6"
p0 "/opt/pf/pfctl -f /tmp/demo-pf.conf && echo 'back to the stateful ruleset'"
echo

bold "--- 7. NAT: the source is translated on the way out, restored on the way back ---"
p0 "cat > /tmp/demo-nat.conf <<EOF
pass out quick on egress from 10.0.1.0/24 to any nat-to ($EGRESS_IF)
pass in quick on ingress proto icmp keep state
pass out quick on egress proto icmp keep state
EOF"
p0 "/opt/pf/pfctl -f /tmp/demo-nat.conf && echo 'nat rules loaded'"
vmc "ping -c2 -W2 $SRV_IP | tail -3"
p0 "/opt/pf/pfctl -ss"
p0 "/opt/pf/pfctl -f /tmp/demo-pf.conf && echo 'back to the stateful ruleset'"
echo

bold "--- 8. divert-to: the packet goes to userspace and comes back ---"
p0 "cat /tmp/demo-divert.conf"
p0 "/opt/pf/pfctl -f /tmp/demo-divert.conf && echo 'divert rule loaded'"
p0 "chmod +x /tmp/pdivert; (setsid /tmp/pdivert -p 10000 -f >/tmp/pdivert.log 2>&1 </dev/null &); sleep 1; pgrep pdivert"
vmc "ping -c2 -W2 10.0.1.1 | tail -3"
p0 "head -3 /tmp/pdivert.log"
echo "  (pdivert flipped the last payload byte and fixed the ICMP checksum)"
echo

bold "--- 9. shutdown ---"
p0 "pkill pdivert; /opt/pf/pfctl -f /dev/null; /opt/pf/pfctl -d; echo 'pf disabled'"
p0 "lsmod | grep '^pf '"
echo
bold "demo over - port, gates and docs:"
bold "  https://git.labexposed.com/lgcosta/pf-linux-port"
echo
