#!/usr/bin/env bash
# Fase 2 - ativacao: carregar pf.ko, habilitar o firewall, ver o estado.
set -u
cd "$(dirname "$0")"
. ./lib.sh

banner "FASE 2 - ATIVACAO"

bold "# o modulo ainda nao esta carregado"
p0 "rmmod pf 2>/dev/null; lsmod | grep '^pf ' || echo 'pf: not loaded'"
p0 "ls -l /dev/pf 2>/dev/null || echo '/dev/pf: ausente enquanto pf nao carrega'"
echo

bold "# carregando o motor PF no kernel"
p0 "insmod /opt/pf/pf.ko && echo 'pf.ko carregado no kernel'"
p0 "dmesg | tail -4"
p0 "lsmod | grep '^pf '"
p0 "ls -l /dev/pf"
echo

bold "# habilitando o firewall"
p0 "/opt/pf/pfctl -e && echo 'pf habilitado'"
p0 "/opt/pf/pfctl -si | head -6"
echo

bold "# o ruleset comeca vazio (a politica padrao do pf e bloquear, com return)"
p0 "/opt/pf/pfctl -sr"
p0 "/opt/pf/pfctl -sf | head -5"
echo
green "  motor ativo: /dev/pf respondendo, pfctl sobre a ABI de ioctl do OpenBSD"
