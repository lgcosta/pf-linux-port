# Pipeline de build em detalhe

English: [docs/en/build.md](../en/build.md).
Receita rápida: [install.md](install.md). Esta página explica *o que cada
etapa faz* e *o que já foi realmente exercitado*.

Tudo abaixo roda no host de build (o laptop); a bancada de VMs (host
separado, "deb") só recebe artefatos — ver [vm-lab.md](vm-lab.md).

## 0. Pré-requisitos

De `docs/pt-BR/install.md`: gcc ≥ 15, GNU make, bison (flex não é
necessário — o `parse.y` do OpenBSD traz um `yylex` escrito à mão), `bc`,
rsync, `glibc-static` para o pfctl estático, root para `insmod`. A árvore do
kernel é esperada em `../linux-7.3.4` (troque com `KERNELDIR=`; o `Makefile`
raiz usa `$(HOME)/lab/linux_kernel/linux-7.3.4`).

## 1. Baixar + verificar as fontes do OpenBSD — `scripts/fetch-sources.sh`

- Baixa `sys.tar.gz` e `src.tar.gz` do mirror primário
  `https://openbsd.c3sl.ufpr.br/pub/OpenBSD/8.0/`, com fallback para
  `cdn.openbsd.org`.
- Confere o sha256 contra os valores de `VERSIONS` (sys: `964b1aed…`, src:
  `9984c74f…`); tarball já em cache com hash correto é pulado.
- Destino: `_vendor/openbsd/` (fora do git).

## 2. Vendorize — `scripts/vendorize.sh`

Extrai **só os arquivos que o port consome** para `_vendor/upstream/`
(fora do git): a lista `sys/net/pf*.c|h`, `ifq/hfsc/fq_codel/ip_pflog/
ip_divert`, `radix/rtable/toeplitz`, `crypto/{md5,sha2,siphash}`,
`sys/sys/*.h` e `sbin/pfctl/*` + headers (arrays `SYS_K`/`SRC_K`). Em
seguida registra o tag `$OpenBSD: …$` de cada arquivo em
`_vendor/upstream/IDS.txt` — a base forense de toda linha portada e de
futuros rebases.

## 3. Staging para o port — `scripts/stage-port.sh` (porting determinístico)

O `stage-port.sh` transforma os arquivos upstream pristinos nos arquivos em
`module/`:

1. todo `#include` é substituído por `pfk.h` + os headers `pfvar` staged
   (os ramos de kernel são selecionados com `-D_KERNEL=1`);
2. o programa de renomeação compartilhado (`pf_sed`) resolve colisões de
   nomes BSD↔Linux (`struct ip` → `struct pf_ip`, `ifnet` → `pfk_ifnet`,
   `ip_output` → `pfk_ip_output`, atribuições a `sa_len` removidas, …);
3. **cada execução é anexada a `docs/porting-notes.md`** — esse arquivo é o
   log de staging, não prosa.

Os arquivos portados são então commitados e editados à mão na árvore; o
re-staging serve só para perícia (`stage-port.sh --diff <arquivo>` compara a
nossa cópia com o texto upstream pristino). Radix/rtable/siphash/toeplitz
ficam de fora do staging automático de propósito (portados à mão).

## 4. Kernel — `scripts/build-kernel.sh` (+ `kernel-config.sh`)

- `kernel-config.sh`: parte de `defconfig` quando não há `.config` e então
  ajusta (idempotente, via `scripts/config` + `make olddefconfig`):
  `VIRTIO_BLK/PCI/NET=y`, `EXT4_FS=y`, `NETFILTER=y`,
  `NETFILTER_ADVANCED`, `NET_SCHED=y`, `INET=y`, `IPV6=y`,
  `SERIAL_8250(+CONSOLE)=y` — ou seja, tudo que o boot direto da VM pf0
  precisa **sem initrd**. `kernel-config.sh test` habilita também
  `DEBUG_INFO` + `DEBUG_KMEMLEAK` para builds de soak.
- `build-kernel.sh`: `make -j(nproc-2) bzImage modules_prepare`, garante que
  `Module.symvers` exista (copia `vmlinux.symvers` — o kbuild não escreve o
  primeiro) e que `scripts/module.lds` exista; depois deposita os artefatos
  de boot em `out/`: `linux-<ver>-bzImage`, `System.map`, `.config`,
  `kernelversion`, `bzImage.sha256`. Termina com
  `KERNEL_BUILD_OK <kernelrelease>`.
- **Hoje não há patch de core**: `patches/kernel/` não tem nenhum
  `000N-*.patch` (ver [patches.md](patches.md)).

## 5. Módulo — `scripts/build-module.sh`

`make -C "$KERNELDIR" M=module` → `module/pf.ko` (um único módulo hoje; o
qdisc `pfq` chega com o M5). O script prepara sozinho `Module.symvers` /
`modules_prepare`. Lista de objetos (`module/Makefile`): engine portado
(`pf.c`, `pf_ioctl.c`, `pf_norm.c`, `pf_if.c`, `pf_lb.c`, `pf_ruleset.c`,
`pf_table.c`, `pf_osfp.c`, `pf_syncookies.c`, `divert_sock.c`) + camada de
compat (shim mbuf, glue de kernel, if/ip4/ip6/pcb/xmit/ifq, crypto, radix,
subr_tree) + `pfk_mod.c`/`pfk_hooks.c`. As flags de compilação definem
`PFLINUX=1` e `_KERNEL=1`. Termina com `MODULE_BUILD_OK`.

`insmod module/pf.ko` cria `/dev/pf` (0600) e registra os hooks netfilter; o
`dmesg` mostra linhas `pfk: …` (ver [caveats.md](caveats.md) para o que o
caminho de dados faz).

## 6. Userland — `make -C tools`

Alvos (de `tools/Makefile`): `pfctl`, `abitest`, `abi`, `test`, `clean`.

- `pfctl` — as fontes do OpenBSD 8.0 compiladas contra os headers de ABI
  copiados; o resultado é um **binário estático** em `tools/out/pfctl`.
  Cada desvio do upstream está listado em `tools/UPSTREAM.md` (7 edições
  marcadas; o resto é o conjunto de headers/fontes de compat).
- `abi` — compila `abitest` (userland) e `abitest-module` (mesma tabela com
  os headers de compat de kernel), despeja as tabelas de tamanho/offset de
  cada struct e as compara com `diff`; divergência = ABI de ioctl quebrada.
  `out/abi-*.tbl` são as entradas de referência.
- `test` — checagens de parse offline: roda `pfctl -n -f` sobre
  `tools/tests/*.conf` (básico / traduções / filas+scrub / anchor / negativa
  `broken.conf`). É userland puro; não precisa de `/dev/pf`.
- `pdivert` — o fonte do relay de divert fica em `tools/pdivert/`; o lado
  kernel (pendente) está na [matriz de features](feature-matrix.md).

## 7. `Makefile` raiz

`make module | tools | kernel | vm | vm-destroy | test | docs | clean` —
wrappers finos sobre os scripts acima. `make test` roda a suíte em VM
([vm-lab.md](vm-lab.md)); `make docs` apenas lista o conjunto bilíngue.

## O que já foi exercitado vs. não

- As etapas 1–6 foram rodadas ao longo do desenvolvimento M0–M2 no host de
  build (há artefatos na árvore: `out/`, `module/pf.ko`, `tools/out/pfctl`),
  e os scripts acima foram verificados linha a linha para este documento.
- Uma execução **de ponta a ponta a partir de clone limpo**
  (`quickstart-check.sh`, tarefa T19 do plano) **ainda não** foi feita;
  trate a sequência exata do install.md como "conforme projetada e conforme
  o último estado por milestone", não como verificada em CI.
