# Aplicando o conjunto de patches PF-on-Linux / guia de compilação

Este documento é a versão em português de `docs/en/install.md`.

Alvo: árvore de fontes do **Linux 7.3.4** + módulos out-of-tree `pf.ko`
(engine PF, família divert(4), filas ifq). O port é entregue como
**árvore de código-fonte + zero patches de core** (ver `patches/kernel/README.md`):
o módulo linka apenas símbolos que o kernel 7.3.4 puro já exporta.

## 1. Pré-requisitos

| Requisito | Por quê |
|---|---|
| gcc >= 15, GNU make, flex, bison, bc | build do kernel + pfctl |
| `glibc-static` (ou musl-static) | pfctl estático para as VMs guest |
| Fontes Linux 7.3.4 | alvo da compilação |
| `sys.tar.gz` + `src.tar.gz` do OpenBSD 8.0 | fontes portados (baixados e verificados por sha256; mirror primário openbsd.c3sl.ufpr.br, fallback cdn.openbsd.org) |
| root no host de build | `insmod` como root |

## 2. Passos

```sh
cd pf_port

# 2.1 busca e verifica as fontes OpenBSD 8.0 (sha256 fixados em VERSIONS)
./scripts/fetch-sources.sh
./scripts/vendorize.sh          # staging em _vendor/upstream + IDS.txt

# 2.2 prepara o kernel (config + build + preparação p/ módulos externos)
KERNELDIR=/path/to/linux-7.3.4 ./scripts/build-kernel.sh
#   - defconfig + virtio/EXT4 embutidos (boot direto do kernel na VM)
#   - make bzImage modules_prepare
#   - copia vmlinux.symvers -> Module.symvers (necessário para builds M=)

# 2.3 aplica os patches de core (atualmente NENHUM é necessário)
./scripts/apply-patches.sh /path/to/linux-7.3.4
#   patches/kernel/series está vazio por projeto; se futuros milestones
#   precisarem de exports, cada um entra como 000N-*.patch documentado em
#   docs/pt-BR/patches.md.

# 2.4 compila módulo e userland
./scripts/build-module.sh        # -> module/pf.ko
make -C tools                    # -> tools/out/pfctl (estático)

# 2.5 instala num sistema 7.3.4 rodando
sudo insmod module/pf.ko         # cria /dev/pf (0600 root)
sudo cp tools/out/pfctl /usr/local/sbin/
sudo pfctl -e                    # habilita
sudo pfctl -f /etc/pf.conf       # sintaxe real do OpenBSD 8.0
```

## 3. Verificação

```sh
pfctl -s info            # "Status: Enabled", contadores ao vivo
pfctl -nf /etc/pf.conf   # checagem de parse apenas
dmesg | grep pfk         # hooks registrados (data path via workqueue netisr)
```

Bancada de VMs (Alpine em libvirt): `./scripts/vm-create.sh`,
`./scripts/vm-deploy.sh`, `./scripts/vm-run-tests.sh` — ver `docs/pt-BR/vm-lab.md`.

## 4. Remoção / reversão

```sh
sudo pfctl -d; sudo rmmod pf
./scripts/unpatch.sh /path/to/linux-7.3.4   # desfaz patches futuros, se houver
```

## 5. O que "aplicar o patch" significa aqui

Como a entrega é um módulo out-of-tree, "aplicar o conjunto" = passos
2.1–2.4; a árvore Linux intacta é apenas *configurada e compilada*
(virtio/EXT4 =y). Nenhum arquivo do kernel é modificado, salvo se um futuro
milestone acrescentar `patches/kernel/000N-*.patch` documentado (cada patch:
uma seção em docs/pt-BR/patches.md com o quê/por quê/impacto/reversão).

## 6. Notas sobre os passos acima (2026-11-11)

- O passo **2.3 é um no-op documentado hoje**: o conjunto de patches está
  vazio e os scripts `scripts/apply-patches.sh` / `scripts/unpatch.sh` e o
  arquivo `patches/kernel/series` ainda não existem na árvore (entram junto
  com o primeiro patch real de core — ver [patches.md](patches.md)). Pode
  pular 2.3 com segurança.
- O passo 2.4 compila apenas o `pfctl` e o verificador de ABI (alvos de
  `make -C tools`: `pfctl abitest abi test clean`). `pdivert` existe como
  arquivo-fonte sem alvo no Makefile por enquanto; `pflogd` ainda não foi
  portado.
- Depois do 2.5, o que dá para usar de verdade é o núcleo de filtragem M2
  (pass/block/quick/keep state/tabelas/anchors); NAT/scrub/queues/divert/
  pflog dependem dos próximos milestones — consulte a
  [matriz de features](feature-matrix.md) e leia [pf-guide.md](pf-guide.md)
  antes de habilitar PF num caminho que você precisa manter vivo.

## 7. Para onde ir agora

| | |
|---|---|
| Arquitetura em uma página | [guide.md](guide.md) |
| Etapas de build em detalhe | [build.md](build.md) |
| Bancada de VMs de validação | [vm-lab.md](vm-lab.md) |
| Regras do dia a dia e avisos | [pf-guide.md](pf-guide.md) |
| Divergências conhecidas | [caveats.md](caveats.md) |
| Mecanismo de patches de core | [patches.md](patches.md) |
| Índice da documentação | [README.md](README.md) |
