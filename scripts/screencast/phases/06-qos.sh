#!/usr/bin/env bash
# Fase 6 - QOS: filas do OpenBSD 8.0 (hfsc) no caminho de dados do Linux.
set -u
cd "$(dirname "$0")"
. ./lib.sh

banner "FASE 6 - QOS (FILAS / ALTQ)"

p0 "/opt/pf/pfctl -e"
p0 "/opt/pf/pfctl -f /dev/null"

IF_SRV=$(onvm "$PF0_MGMT" "ip route get $SRV_IP | grep -o 'dev [a-z0-9]*' | cut -d' ' -f2" | tr -d '\r')
[ -n "$IF_SRV" ] || IF_SRV=eth2

bold "# a sintaxe de filas do OpenBSD 8.0: queue com classes hfsc"
cat > /tmp/sc-q.conf <<CONF
queue rootq on $IF_SRV bandwidth 10Mbit
queue leafq on $IF_SRV bandwidth 2Mbit parent rootq default
pass in quick proto tcp from $CLI_IP to $SRV_IP port 9095 queue leafq keep state
pass out quick keep state
CONF
put /tmp/sc-q.conf >/dev/null
p0 "cat /tmp/sc-q.conf"
p0 "/opt/pf/pfctl -f /tmp/sc-q.conf && echo 'filas carregadas'"
p0 "/opt/pf/pfctl -sr"
echo

bold "# o pfctl mostra as classes e as estatisticas do escalonador"
p0 "/opt/pf/pfctl -s queue -v"
echo

bold "# transferencia de 2 MB pela fila: o trafego entra na classe"
vms "head -c 2000000 /dev/zero | tr '\\0' A > /tmp/sc_data.bin; ls -l /tmp/sc_data.bin; pkill nc 2>/dev/null; (setsid nc -l -p 9095 < /tmp/sc_data.bin >/dev/null 2>&1 &); sleep 1; echo 'servidor escutando na 9095'"
vmc "nc -w60 $SRV_IP 9095 > /tmp/sc_out.bin; wc -c < /tmp/sc_out.bin"
sleep 1
p0 "/opt/pf/pfctl -s queue -v"
echo

bold "# a disciplina hfsc contou o trafego da classe"
p0 "/opt/pf/pfctl -sr -vv | head -6"
p0 "ip -s link show $IF_SRV | head -4"
echo

p0 "/opt/pf/pfctl -f /dev/null; pkill nc 2>/dev/null; /opt/pf/pfctl -s queue -v"
green "  filas do OpenBSD 8.0 (hfsc) anexadas a interface e contando pacotes"
