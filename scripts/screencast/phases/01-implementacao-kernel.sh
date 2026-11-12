#!/usr/bin/env bash
# Fase 1 - a implementacao no kernel: da fonte OpenBSD ao modulo pf.ko.
# Roda no host de build (o port esta no repositorio; a VM so recebe o .ko).
set -u
cd "$(dirname "$0")"
. ./lib.sh

banner "FASE 1 - A IMPLEMENTACAO NO KERNEL"

bold "# o port: fontes BSD trazidos do OpenBSD 8.0 + camada de compatibilidade"
dim "  module/pfsrc/    - fontes do OpenBSD (pf.c, pf_lb.c, pf_norm.c, hfsc.c, ...)"
dim "  module/compat/   - shims que traduzem as APIs BSD para o Linux"
dim "  module/include/  - cabecalhos (pfk.h) e os headers vendorizados do OpenBSD"
echo
cyan "host# ls module/pfsrc | head -20"
ls "$REPO/module/pfsrc" | head -20
echo
cyan "host# wc -l module/pfsrc/*.c | tail -3"
(cd "$REPO" && wc -l module/pfsrc/*.c | tail -3)
cyan "host# wc -l module/compat/*.c | tail -1"
(cd "$REPO" && wc -l module/compat/*.c | tail -1)
echo
dim "  (as fontes OpenBSD entram praticamente intactas; a compat faz a ponte"
dim "   mbuf->sk_buff, spl->spinlock, epoch->RCU, ifnet->net_device, ...)"

bold
bold "# compilando o modulo contra o kernel do lab (build limpo)"
echo
cyan "host# ./scripts/build-module.sh clean"
(cd "$REPO" && ./scripts/build-module.sh clean 2>&1 | tail -3)
cyan "host# ./scripts/build-module.sh"
(cd "$REPO" && ./scripts/build-module.sh 2>&1 | tail -18)
echo
cyan "host# ls -la module/pf.ko"
ls -la "$REPO/module/pf.ko"
cyan "host# modinfo module/pf.ko | head -5"
modinfo "$REPO/module/pf.ko" 2>/dev/null | head -5 || \
	(cd "$REPO" && strings module/pf.ko | grep -m2 vermagic)
echo
green "  modulo pronto: o motor PF 8.0 do OpenBSD pronto para carregar no Linux"
