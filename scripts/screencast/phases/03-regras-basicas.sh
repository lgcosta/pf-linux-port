#!/usr/bin/env bash
# Fase 3 - regras basicas: tabelas, anchors, keep state, block/pass.
set -u
cd "$(dirname "$0")"
. ./lib.sh

banner "FASE 3 - REGRAS BASICAS"

p0 "/opt/pf/pfctl -e"
p0 "/opt/pf/pfctl -f /dev/null"

bold "# um pf.conf de verdade, sintaxe do OpenBSD"
cat > /tmp/sc-regras.conf <<'CONF'
# --- regras basicas do PF ---
set block-policy return

table <trusted> { 10.0.1.0/24 }

anchor "demo"

pass in quick on ingress proto icmp from <trusted> keep state
pass out quick on egress proto icmp keep state

pass in quick
pass out quick
CONF
put /tmp/sc-regras.conf >/dev/null
p0 "cat /tmp/sc-regras.conf"
p0 "/opt/pf/pfctl -f /tmp/sc-regras.conf && echo 'ruleset carregado'"
echo

bold "# o que o pf entendeu"
p0 "/opt/pf/pfctl -sr"
p0 "/opt/pf/pfctl -s Tables"
p0 "/opt/pf/pfctl -t trusted -T show"
echo

bold "# trafego permitido: passa e cria estado"
vmc "ping -c2 -W2 $SRV_IP | tail -3"
p0 "/opt/pf/pfctl -ss | head -5"
echo

bold "# a mesma regra com 'block drop' bloqueia de verdade"
cat > /tmp/sc-block.conf <<CONF
set block-policy return
table <trusted> { 10.0.1.0/24 }
block drop in quick on ingress proto icmp from <trusted>
pass in quick
pass out quick
CONF
put /tmp/sc-block.conf >/dev/null
p0 "/opt/pf/pfctl -f /tmp/sc-block.conf && echo 'ruleset de bloqueio carregado'"
vmc "ping -c2 -W2 $SRV_IP | tail -3"
p0 "/opt/pf/pfctl -sr -vv | head -4"
echo

bold "# anchors: regras aninhadas, como no OpenBSD"
p0 "cat > /tmp/sc-anchor.conf <<'EOF'
pass in quick on ingress proto icmp from 10.0.1.10 to 10.0.2.20 keep state
pass in quick
pass out quick
EOF"
p0 "/opt/pf/pfctl -a demo -f /tmp/sc-anchor.conf && echo 'ruleset do anchor carregado'"
p0 "/opt/pf/pfctl -a demo -sr"
p0 "cat > /tmp/sc-main.conf <<'EOF'
anchor \"demo\"
pass in quick on ingress proto icmp keep state
pass out quick on egress proto icmp keep state
EOF"
p0 "/opt/pf/pfctl -f /tmp/sc-main.conf && echo 'ruleset principal com anchor carregado'"
p0 "/opt/pf/pfctl -sr"
echo

bold "# voltando ao ruleset com estado"
p0 "/opt/pf/pfctl -f /tmp/sc-regras.conf && echo 'ruleset stateful restaurado'"
vmc "ping -c2 -W2 $SRV_IP | tail -2"
green "  tabelas, anchors, quick, keep state, block drop: todos no lugar"
