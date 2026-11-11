# PF no Linux

Port do **PF** (packet filter) do OpenBSD para o **Linux 7.3.4** como um
módulo de kernel out-of-tree — o engine PF real (`pf.c`, `pf_norm.c`,
`pf_table.c`, `pf_lb.c`, … portados dos fontes do OpenBSD 8.0) controlado
pelo `pfctl` real do OpenBSD via a ABI ioctl de `/dev/pf`. (Um segundo
módulo para o qdisc `pfq` chega com o M5.)

**Status:** milestones iniciais. O engine portado e o plano de controle (M1)
funcionam; o núcleo de filtragem do M2 está em validação na bancada de VMs;
todo o resto está pendente. Status honesto por feature:
[`docs/pt-BR/feature-matrix.md`](docs/pt-BR/feature-matrix.md).
Design: [`docs/superpowers/specs/2026-11-11-pf-linux-port-design.md`](docs/superpowers/specs/2026-11-11-pf-linux-port-design.md)
· plano: [`docs/superpowers/plans/2026-11-11-pf-linux-port.md`](docs/superpowers/plans/2026-11-11-pf-linux-port.md).

## Milestones

| Milestone | Escopo | Status (2026-11-11) |
|---|---|---|
| M0 | repo, scripts, build do kernel, VM sobe com kernel próprio | concluído |
| M1 | camada de compat, `pf.ko` carrega, `/dev/pf`, `pfctl -f` / `-s info` | concluído |
| M2 | núcleo de filtragem: regras/estados/tabelas/anchors, gates VM 01–04 | **em andamento** — caminho de dados implementado, gates ainda não todos verdes |
| M3 | NAT (nat/rdr/binat), scrub e2e, pflog | pendente |
| M4 | route-to/dup-to/fail-to, divert e2e | pendente |
| M5 | filas: back ends HFSC/FQ-CoDel + qdisc `pfq` | pendente |
| M6 | docs completas, patchset arrumado, suíte inteira verde, release com tag | em andamento (docs) |

## O que você obtém (planejado)

| Área | Recurso |
|---|---|
| Filtragem | regras, anchors, quick, `match`/`block`(drop/return/return-rst)/`pass`, `keep state`, syncookies |
| Estado/Tables | firewall stateful, tables `pfr` (radix + hash, `pfctl -t`) |
| NAT | `nat-to` (pools de portas, sticky-port, static-port), `rdr-to`, `binat-to` |
| Scrub | remonta­gmento de fragmentos, drop por overlap, `min-ttl`/`max-mss`/`df`/`no-df`, `set reassemble` |
| QoS (sucessor do ALTQ) | `queue`/`queue on` com os agendadores `ifq` do OpenBSD 8.0: **default (priq), HFSC, FQ-CoDel**, anexados via qdisc `pfq`; estatísticas `pfctl -s queue -v` |
| Divert | `divert-to` / tag `divert` + relay `pdivert` sobre uma família de sockets `PF_DIVERT` dentro do módulo (pendente — ver `docs/pt-BR/feature-matrix.md`) |
| Roteamento | `route-to` (pools rr/hash-source/hash-state, `fail-to`), `reply-to`, `dup-to` |
| Logging | `pflogd` via canal netlink pflog (pcap DLT_PFLOG) |
| Controle | `/dev/pf` (0600), ABI ioctl do OpenBSD 8.0, `pfctl` compilado para Linux (binário único) |

Fora de escopo: failover `pfsync`/CARP, IPsec/pfkey, `ftp-proxy`, instâncias PF
por netns, agendadores exclusivos do ALTQ clássico (cbq/wfq/red — removidos do
OpenBSD antes da 8.0).

## Início rápido

Receita completa: [`docs/pt-BR/install.md`](docs/pt-BR/install.md). Versão
curta (comandos lidos dos scripts; a execução de ponta a ponta a partir de
clone limpo ainda está pendente):

```sh
./scripts/fetch-sources.sh          # fontes OpenBSD 8.0 (mirror C3SL, sha256 fixado)
./scripts/vendorize.sh              # staging dos arquivos upstream portados
./scripts/build-kernel.sh           # kernel 7.3.4: bzImage + modules_prepare (patch de export: nenhum necessário)
./scripts/build-module.sh           # -> module/pf.ko
make -C tools                       # -> tools/out/pfctl (estático), verificador de ABI
sudo insmod module/pf.ko            # /dev/pf (0600) + hooks netfilter
sudo pfctl -f /etc/pf.conf && sudo pfctl -e
```

Leia [`docs/pt-BR/pf-guide.md`](docs/pt-BR/pf-guide.md) **antes** de habilitar
o PF num caminho que você precisa: só o núcleo de filtragem do M2 tem gate
hoje; NAT/scrub/filas/divert/pflog ainda não são utilizáveis
([`docs/pt-BR/feature-matrix.md`](docs/pt-BR/feature-matrix.md)).

Lab de validação em VMs (Alpine via libvirt): [`docs/pt-BR/vm-lab.md`](docs/pt-BR/vm-lab.md).
English documentation: [`README.md`](README.md) e
[`docs/en/`](docs/en/README.md).

## Estrutura

- `module/` — fontes de kernel (PF portado + camada de compat; o qdisc `pfq` chega com o M5)
- `tools/` — pfctl (build estático Linux), fonte do relay pdivert, verificador de ABI (`pflogd` ainda não portado)
- `patches/kernel/` — conjunto de patches de core do Linux (hoje vazio; evidência em `p1-needs.txt`, ver `docs/pt-BR/patches.md`)
- `scripts/` — automação de fetch/build/patch/VM
- `tests/` — suíte em VM (`tests/vm/NN-*.sh`, helpers scapy)
- `docs/en/` + `docs/pt-BR/` — documentação completa bilíngue
- `docs/superpowers/` — design + plano de implementação

## Licença

O código portado do OpenBSD mantém as licenças e avisos originais
([ISC](LICENSE)). Código novo do lado Linux: ISC.
