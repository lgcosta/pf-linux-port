#!/usr/bin/env bash
# Fase 4 - roteamento: route-to (caminho forcado) e reply-to (assimetria).
set -u
cd "$(dirname "$0")"
. ./lib.sh

banner "FASE 4 - ROTEAMENTO"

p0 "/opt/pf/pfctl -e"
p0 "/opt/pf/pfctl -f /dev/null"
put ./an.sh >/dev/null

IF_SRV=$(onvm "$PF0_MGMT" "ip route get $SRV_IP | grep -o 'dev [a-z0-9]*' | cut -d' ' -f2" | tr -d '\r')
IF_GW=$(onvm "$PF0_MGMT" "ip route get $GW2_IP | grep -o 'dev [a-z0-9]*' | cut -d' ' -f2" | tr -d '\r')
[ -n "$IF_SRV" ] || IF_SRV=eth2
[ -n "$IF_GW" ] || IF_GW=eth3
dim "  (o servidor fica atras de $IF_SRV; o segundo gateway, em $IF_GW)"

bold "# route-to: o pacote sai pelo caminho forcado, nao pelo normal"
cat > /tmp/sc-route.conf <<CONF
pass in quick proto icmp from $CLI_IP to $SRV_IP route-to { $GW2_IP } keep state
pass out quick
CONF
put /tmp/sc-route.conf >/dev/null
p0 "/opt/pf/pfctl -f /tmp/sc-route.conf && echo 'regra route-to carregada'"
p0 "/opt/pf/pfctl -sr"
p0 "pkill tcpdump 2>/dev/null; (tcpdump -ni $IF_GW -c 4 -e icmp > /tmp/sc_gw.td 2>&1 &); (tcpdump -ni $IF_SRV -c 4 -e icmp > /tmp/sc_srv.td 2>&1 &); sleep 1"
vmc "ping -c2 -W2 $SRV_IP | tail -2"
dim "  (o ping falha de proposito: o pacote foi entregue ao gateway $GW2_IP)"
sleep 3
p0 "echo \"egresso forcado ($IF_GW):\"; sh /tmp/an.sh /tmp/sc_gw.td req"
p0 "echo \"egresso normal ($IF_SRV):\"; sh /tmp/an.sh /tmp/sc_srv.td req"
echo

bold "# reply-to: a resposta volta por um caminho diferente do pedido"
cat > /tmp/sc-reply.conf <<CONF
pass in quick proto icmp from $CLI_IP to $SRV_IP reply-to { $GW2_IP } keep state
pass out quick
CONF
put /tmp/sc-reply.conf >/dev/null
p0 "/opt/pf/pfctl -f /tmp/sc-reply.conf && echo 'regra reply-to carregada'"
p0 "/opt/pf/pfctl -sr"
p0 "pkill tcpdump 2>/dev/null; (tcpdump -ni $IF_GW -c 6 -e icmp > /tmp/sc_rgw.td 2>&1 &); (tcpdump -ni eth1 -c 6 -e icmp > /tmp/sc_rcli.td 2>&1 &); sleep 1"
vmc "ping -c2 -W2 $SRV_IP | tail -2"
dim "  (de novo sem resposta direta: a resposta foi forcada para o gateway)"
sleep 3
p0 "echo \"resposta forcada ($IF_GW):\"; sh /tmp/an.sh /tmp/sc_rgw.td rep"
p0 "echo \"resposta pelo caminho normal do cliente:\"; sh /tmp/an.sh /tmp/sc_rcli.td rep"
echo

bold "# rota normal restaurada"
p0 "/opt/pf/pfctl -f /dev/null; /opt/pf/pfctl -sr"
p0 "pkill tcpdump 2>/dev/null; echo 'capturas encerradas'"
green "  route-to e reply-to: o proximo salto e decidido pelo pf, nao pela tabela de rotas"
