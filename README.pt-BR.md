# PF no Linux

Port do **PF** (packet filter) do OpenBSD para o **Linux 7.3.4** como um
módulo de kernel out-of-tree — o engine PF real (`pf.c`, `pf_norm.c`,
`pf_table.c`, `pf_lb.c`, `hfsc.c`, … portados dos fontes do OpenBSD 8.0)
controlado pelo `pfctl` real do OpenBSD via a ABI ioctl de `/dev/pf`.

**Status:** o núcleo de filtragem, a filtragem stateful, tabelas, anchors, NAT
(`nat-to`/`rdr-to`/`binat-to`), roteamento forçado (`route-to`/`dup-to`/
`reply-to`), pools, `divert`, `pflog` e as **filas do OpenBSD 8.0 (HFSC)** estão
**verificados num lab libvirt real** — 16/16 gates verdes. O que ainda está
pendente está dito por feature em
[`docs/pt-BR/feature-matrix.md`](docs/pt-BR/feature-matrix.md).

Screencasts (EN + PT, com narração): [`docs/pt-BR/screencast.md`](docs/pt-BR/screencast.md).
Design: [`docs/superpowers/specs/2026-11-11-pf-linux-port-design.md`](docs/superpowers/specs/2026-11-11-pf-linux-port-design.md)
· plano: [`docs/superpowers/plans/2026-11-11-pf-linux-port.md`](docs/superpowers/plans/2026-11-11-pf-linux-port.md).

## Milestones

| Milestone | Escopo | Status |
|---|---|---|
| M0 | repo, scripts, build do kernel, VM sobe com kernel próprio | concluído |
| M1 | camada de compat, `pf.ko` carrega, `/dev/pf`, `pfctl -f` / `-s info` | concluído |
| M2 | núcleo de filtragem: regras/estados/tabelas/anchors, gates VM 01–04 | **concluído** — gates 01/02/03/04 verdes |
| M3 | NAT (`nat-to`/`rdr-to`/`binat-to`), scrub | **concluído** — gates 05/06/07/09 (incl. 2 MB íntegros através de um redirect, min-ttl/max-mss medidos) |
| M4 | route-to/dup-to/reply-to, pools, divert e2e, pflog | **concluído** — divert (11), pflog (12), route-to/dup-to (08), pools (10), reply-to (13) verificados |
| M5 | filas: back end HFSC no caminho de dados | **concluído (mecânico)** — gate 15: as filas carregam, o hfsc é anexado, `pfctl -s queue -v` mostra as estatísticas das classes, e um fluxo de 2 MB pela fila é contado pela classe. `bandwidth` = linkshare do HFSC (garantia, não teto; o 8.0 não tem sintaxe de upperlimit) |
| M6 | docs completas, patchset arrumado, suíte inteira verde, release com tag | **concluído** — docs EN+PT-BR, 16/16 da suíte verdes, tag `v1.0.0` |

## O que você obtém

| Área | Recurso |
|---|---|
| Filtragem | regras, anchors, quick, `match`/`block`(drop/return/return-rst)/`pass`, `keep state`, syncookies |
| Estado/Tables | firewall stateful, tables `pfr` (radix + hash, `pfctl -t`) |
| NAT | `nat-to` (pools de portas, sticky-port, static-port), `rdr-to`, `binat-to` |
| Scrub | remontagem de fragmentos, drop por overlap, `min-ttl`/`max-mss`/`df`/`no-df`, `set reassemble` |
| QoS (sucessor do ALTQ) | `queue`/`queue on` com os agendadores `ifq` do OpenBSD 8.0: **HFSC verificado no caminho de dados** (gate 15), priq como default, FQ-CoDel compilado; `pfctl -s queue -v` funciona |
| Divert | `divert-to` / tag `divert` + relay `pdivert` sobre uma família de sockets `PF_DIVERT` dentro do módulo (verificado: pacote vai para o userspace e volta) |
| Roteamento | `route-to`/`dup-to`/`reply-to` + pools (`round-robin`, `source-hash`) verificados (`fail-to` não existe mais no OpenBSD 8.0) |
| Logging | `pflogd` via `/dev/pflog` (pcap DLT_PFLOG, layout de registro do OpenBSD) — verificado |
| Controle | `/dev/pf` (0600), ABI ioctl do OpenBSD 8.0, `pfctl` compilado para Linux (binário único) |

Fora de escopo: failover `pfsync`/CARP, IPsec/pfkey, `ftp-proxy`, instâncias PF
por netns, agendadores exclusivos do ALTQ clássico (cbq/wfq/red — removidos do
OpenBSD antes da 8.0).

## Screencasts

Sete screencasts narrados por idioma (inglês e português), 1080p30 com
legendas queimadas, o diagrama de rede do lab desenhado sobre o terminal e um
filme contínuo que emenda as fases com transições. Cada vídeo é uma **sessão
shell real** contra as VMs Alpine — comandos digitados tecla a tecla, o kernel
respondendo ao vivo — com narração de voz feminina gerada pelo MiniMax:

```sh
./scripts/screencast/build-all.sh       # -> out/screencast/{en,pt}/*.mp4
```

Detalhes, quebra por fase e o texto de descrição do YouTube:
[`docs/pt-BR/screencast.md`](docs/pt-BR/screencast.md) ·
[`docs/en/screencast.md`](docs/en/screencast.md)

## Início rápido

Receita completa: [`docs/pt-BR/install.md`](docs/pt-BR/install.md). **Do zero no
Debian 13 (trixie)** (preparar host → kernel → módulo → ferramentas → lab →
stress): [`docs/pt-BR/debian13.md`](docs/pt-BR/debian13.md). Versão
curta (comandos lidos dos scripts):

```sh
./scripts/fetch-sources.sh          # fontes OpenBSD 8.0 (mirror C3SL, sha256 fixado)
./scripts/vendorize.sh              # staging dos arquivos upstream portados
./scripts/build-kernel.sh           # kernel 7.3.4: bzImage + modules_prepare (patch de export: nenhum necessário)
./scripts/build-module.sh           # -> module/pf.ko
make -C tools                       # -> tools/out/{pfctl,pdivert,pflogd,abitest}
sudo insmod module/pf.ko            # /dev/pf (0600) + hooks netfilter
sudo pfctl -f /etc/pf.conf && sudo pfctl -e
```

Toda feature listada acima é exercitada pela suíte do lab antes de você
precisar dela; [`docs/pt-BR/feature-matrix.md`](docs/pt-BR/feature-matrix.md)
diz qual gate prova o quê, e [`docs/pt-BR/pf-guide.md`](docs/pt-BR/pf-guide.md)
lista as divergências que você deve conhecer antes de colocar o PF num
caminho que importa.

Lab de validação em VMs (Alpine via libvirt): [`docs/pt-BR/vm-lab.md`](docs/pt-BR/vm-lab.md).
English documentation: [`README.md`](README.md) e
[`docs/en/`](docs/en/README.md).

## Estrutura

- `module/` — fontes de kernel: engine PF portado (`module/pfsrc`) + camada de compat Linux (`module/compat`)
- `tools/` — pfctl (build estático Linux), relay pdivert, pflogd, verificador de ABI (`make -C tools abi`)
- `patches/kernel/` — conjunto de patches de core do Linux (hoje vazio; evidência em `p1-needs.txt`, ver `docs/pt-BR/patches.md`)
- `scripts/` — automação de fetch/build/patch/VM, mais `scripts/screencast/` para os vídeos
- `scripts/debian13/` — preparação do host Debian 13 (`prepare-host.sh`)
- `scripts/stress/` — runner do stress (`run-stress.sh`) + gerador de relatório
- `tests/vm/` — suíte em VM: 16 gates (`tests/vm/NN-*.sh`) + `lib.sh`
- `docs/en/` + `docs/pt-BR/` — documentação completa bilíngue
- `docs/superpowers/` — design + plano de implementação

## Licença

O código portado do OpenBSD mantém as licenças e avisos originais
([ISC](LICENSE)). Código novo do lado Linux: ISC.
