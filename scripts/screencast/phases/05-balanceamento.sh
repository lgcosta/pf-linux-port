#!/usr/bin/env bash
# Fase 5 - balanceamento: pools (round-robin, source-hash) e dup-to.
set -u
cd "$(dirname "$0")"
. ./lib.sh

banner "FASE 5 - BALANCEAMENTO"

p0 "/opt/pf/pfctl -e"
p0 "/opt/pf/pfctl -f /dev/null"
put ./an.sh >/dev/null

IF_SRV=$(onvm "$PF0_MGMT" "ip route get $SRV_IP | grep -o 'dev [a-z0-9]*' | cut -d' ' -f2" | tr -d '\r')
IF_GW=$(onvm "$PF0_MGMT" "ip route get $GW2_IP | grep -o 'dev [a-z0-9]*' | cut -d' ' -f2" | tr -d '\r')
[ -n "$IF_SRV" ] || IF_SRV=eth2
[ -n "$IF_GW" ] || IF_GW=eth3

bold "# pool round-robin: a traducao de origem alterna entre os membros do pool"
cat > /tmp/sc-pool.conf <<CONF
pass out quick on egress from 10.0.1.0/24 to any nat-to { 10.0.2.1, 10.0.3.1 } round-robin
pass in quick on ingress proto icmp keep state
pass out quick on egress proto icmp keep state
CONF
put /tmp/sc-pool.conf >/dev/null
p0 "cat /tmp/sc-pool.conf"
p0 "/opt/pf/pfctl -f /tmp/sc-pool.conf && echo 'pool round-robin carregado'"
p0 "/opt/pf/pfctl -sr"
p0 "pkill tcpdump 2>/dev/null; (tcpdump -ni $IF_SRV -c 12 -e icmp > /tmp/sc_pool.td 2>&1 &); sleep 1"
for i in 1 2 3 4 5 6; do
	vmc "ping -c1 -W2 $SRV_IP >/dev/null 2>&1; echo '   pacote $i enviado'"
done
sleep 4
p0 "echo 'origem vista pelo servidor (o pool alterna entre os membros):'; sh /tmp/an.sh /tmp/sc_pool.td src"
echo

bold "# pool source-hash: a mesma origem cai sempre no mesmo membro"
cat > /tmp/sc-hash.conf <<CONF
pass out quick on egress from 10.0.1.0/24 to any nat-to { 10.0.2.1, 10.0.3.1 } source-hash
pass in quick on ingress proto icmp keep state
pass out quick on egress proto icmp keep state
CONF
put /tmp/sc-hash.conf >/dev/null
p0 "/opt/pf/pfctl -f /tmp/sc-hash.conf && echo 'pool source-hash carregado'"
p0 "pkill tcpdump 2>/dev/null; (tcpdump -ni $IF_SRV -c 8 -e icmp > /tmp/sc_hash.td 2>&1 &); sleep 1"
for i in 1 2 3 4; do
	vmc "ping -c1 -W2 $SRV_IP >/dev/null 2>&1; echo '   pacote $i enviado'"
done
sleep 4
p0 "echo 'origem com source-hash (estavel por origem):'; sh /tmp/an.sh /tmp/sc_hash.td src"
echo

bold "# dup-to: o pacote segue o caminho normal e uma copia vai para o outro gateway"
cat > /tmp/sc-dup.conf <<CONF
pass in quick proto icmp from $CLI_IP to $SRV_IP dup-to { $GW2_IP } keep state
pass out quick
CONF
put /tmp/sc-dup.conf >/dev/null
p0 "/opt/pf/pfctl -f /tmp/sc-dup.conf && echo 'regra dup-to carregada'"
p0 "/opt/pf/pfctl -sr"
p0 "pkill tcpdump 2>/dev/null; (tcpdump -ni $IF_SRV -c 4 -e icmp > /tmp/sc_dup1.td 2>&1 &); (tcpdump -ni $IF_GW -c 4 -e icmp > /tmp/sc_dup2.td 2>&1 &); sleep 1"
vmc "ping -c2 -W2 $SRV_IP | tail -2"
sleep 4
p0 "echo \"caminho normal ($IF_SRV):\"; sh /tmp/an.sh /tmp/sc_dup1.td req"
p0 "echo \"copia espelhada ($IF_GW):\"; sh /tmp/an.sh /tmp/sc_dup2.td req"
echo

p0 "/opt/pf/pfctl -f /dev/null; pkill tcpdump 2>/dev/null"
green "  round-robin reparte a carga; source-hash fixa por origem; dup-to replica"
