# Guia de arquitetura — uma página

English: [docs/en/guide.md](../en/guide.md).

## O que este port é

Um port fiel do **engine PF do OpenBSD 8.0** rodando dentro do **Linux
7.3.4** como um único módulo out-of-tree, `pf.ko`. Nada é simulado: o
casamento de regras, estado, tabelas, normalização e filas *é* o código do
OpenBSD, compilado sob uma camada de compatibilidade; o controle vem do
`pfctl` real, falando a ABI ioctl OpenBSD de verdade por `/dev/pf`. Existe
**uma instância global do PF, presa a `init_net`** — exatamente como o
OpenBSD tem um único kernel; instâncias por netns estão fora de escopo.

Alvos de build: fontes OpenBSD 8.0 (sha256 fixado em `VERSIONS`), Linux
7.3.4 (`linux-7.3.4`), `gcc >= 15`, bison. O `pfvar.h` v1.548 é compartilhado
bit a bit entre kernel e userland (codificação de ioctl em
`module/include/pfk.h`).

## Componentes

| Caminho | Papel |
|---|---|
| `module/pfsrc/` | Arquivos portados do OpenBSD: `pf.c` (engine), `pf_ioctl.c` (ABI DIOC*), `pf_norm.c` (scrub/fragmentos), `pf_table.c`, `pf_ruleset.c`, `pf_if.c`, `pf_lb.c` (pools/portas NAT), `pf_osfp.c`, `pf_syncookies.c`, `hfsc.c`/`fq_codel.c` (back ends de fila, no módulo), `divert_sock.c` (família AF_DIVERT), `pfk_pflog.c` (back end do pflog), `pfk_mod.c` (load/exit, `/dev/pf`), `pfk_hooks.c` (caminho de dados netfilter) |
| `module/compat/` | Infra Linux sob os fontes em forma BSD: shim mbuf-sobre-`sk_buff` (`pfk_mbuf`), glue de kernel (malloc/pools/timeouts/locks, `pfk_kern`), visão de interfaces e endereços (`pfk_if`), saída/forward IP com semântica BSD (`pfk_ip4`, `pfk_ip6`), busca de sockets (`pfk_pcb`), API ifq com semântica priq (`pfk_ifq`), radix/rotas/crypto portados |
| `module/include/` | `pfk.h` (header mestre da compat + codificação `_IO*` estilo OpenBSD), `net/pfvar.h` staged, `pfk_mbuf.h`, `pfk_net.h`, `pfk_if.h` |
| `tools/` | Build Linux do `pfctl` do OpenBSD 8.0 (binário único), relay `pdivert`, verificador de ABI (`abitest` + `make abi`), testes de parse offline (`make test`). Deltas vs upstream: `tools/UPSTREAM.md` |
| `scripts/` | `fetch-sources.sh`, `vendorize.sh`, `stage-port.sh` (staging determinístico), `build-kernel.sh`, `build-module.sh`, automação da bancada `vm-*.sh` |
| `tests/vm/` | Gates da suíte em VM: `01-state.sh … 04-anchors.sh` + `lib.sh`; `tests/confs/` com rulesets deployados na bancada |
| `patches/kernel/` | Conjunto de patches de core — hoje **vazio** (`p1-needs.txt` registra por que nenhum export foi necessário); ver [patches.md](patches.md) |

## Caminho de dados (M2, como implementado hoje)

O OpenBSD roda o PF em `IPL_NET`, em contexto de processo; nossas locks da
compat são sleepáveis, então o hook **nunca chama `pf_test()` em softirq**. O
pacote é furtado (`NF_STOLEN`) para uma workqueue ordenada (`pfk_netisr`), o
veredito roda em contexto de processo e é aplicado por **reinjeção**, com
marcação por direção em `skb->mark` para que o pacotão sobrevivente não seja
re-enfileirado pelo nosso próprio hook.

```
                        Linux 7.3.4, módulo pf.ko
  NIC ──► netfilter            (softirq — apenas enfileira)
        PRE_ROUTING  PF_IN ─┐                    ┌─ LOCAL_OUT   PF_OUT
        POST_ROUTING PF_OUT ─┤  NF_STOLEN        ├─ (roda DEPOIS do conntrack)
        LOCAL_IN/FORWARD: pass-through           ┘   POST_ROUTING: NF_IP_PRI_LAST-1
                              │  (ref da skb + kmalloc pfk_pkt;
                              │   falha de alocação ⇒ NF_ACCEPT = fail-open)
                              ▼
                  workqueue pfk_netisr  (contexto de processo)
                              │
                  pfk_m_wrap_skb(skb)──► shim struct mbuf
                              │
                          pf_test(af, dir, ifp, &m)   ◄── o engine real do OpenBSD 8.0:
                              │                          scrub/normalize → lookup de estado
                              │                          → regras/anchors/tabelas/quick
                              │                          → inserção de estado → veredito
                              ▼
        pass ──► mark |= PFK_DONE_IN/OUT ──► reinjeta:  PF_IN: netif_rx() (sobrevivente
                                                      da remontagem entra no stack)
                                                  PF_OUT: skb_dst()->output()
        block/drop ──► kfree_skb()  (+ RST/ICMP de retorno gerados pelo próprio PF)
        fragmento em buffer ──► nossa ref é liberada; pf mantém o sobrevivente (m == NULL)
```

Plano de controle: `/dev/pf` (miscdev, modo 0600, escrita condicionada a
`CAP_NET_ADMIN`) → `pfk_ioctl_io()` copia a estrutura de parâmetros usando o
tamanho embutido na codificação `_IOR/_IOW` OpenBSD e despacha para o
`pfioctl()` portado (`pf_ioctl.c`) — a ABI DIOC* completa, como upstream.
`pfctl -e/-f/-s/-t` é o mesmo binário do OpenBSD, recompilado.

## Filas, divert, pflog — comportamento observável hoje

- **Filas**: o `pfctl` faz o parse da sintaxe `queue`/`queue on` do OpenBSD 8.0
  e o kernel a implementa na forma do BSD: `compat/pfk_ifq.c` fornece as
  interfaces `ifq_ops`/`pfq_ops` e os back ends do OpenBSD (`pfsrc/hfsc.c`,
  `pfsrc/fq_codel.c`) se plugam nelas; **priq** é a disciplina default. O
  gate `15-queue.sh` prova a disciplina **hfsc** de ponta a ponta: a
  configuração de filas carrega, a disciplina é anexada à interface, o
  `pfctl -s queue -v` lista as duas classes com estatísticas, um fluxo de 2 MB
  pela fila completa e o contador de pacotes da classe avança. Os handles de
  classe e as structs de estatísticas vêm do próprio `net/hfsc.h` e
  `net/fq_codel.h` do OpenBSD, vendorizados para que kernel e `pfctl` não
  puedan divergir.
- **Divert**: `divert-to`/tag `divert` produzem o veredito `PF_DIVERT` do PF e
  o módulo registra uma família de sockets `AF_DIVERT` (`PF_DIVERT=32`) em
  runtime — sem patch no kernel. Sem listener o pacote é descartado
  (`divs_noport`, paridade com o BSD); com o relay `pdivert` rodando o pacote
  vai para o userspace, é reescrito e reinjetado. Gate `11-divert.sh`.
- **pflog**: o engine enfileira os pacotes logados, `pfsrc/pfk_pflog.c` os
  expõe por `/dev/pflog` e o `tools/pflogd` escreve um pcap DLT_PFLOG cujo
  layout de registro é byte a byte compatível com o `pflog(4)` do OpenBSD.
  Gate `12-pflog.sh`. A divergência em relação ao upstream é o transporte (um
  chardev em vez de uma interface `if_pflog`/bpf), não o formato.

## Milestones (status por feature em feature-matrix.md)

| Milestone | Escopo | Status |
|---|---|---|
| M0 | repo, scripts, build do kernel, VM liga com kernel próprio | concluído |
| M1 | camada de compat, `pf.ko` carrega, `/dev/pf`, `pfctl -f`/`-s info` | concluído |
| M2 | núcleo do filtro: regras/estados/tabelas/âncoras, gates VM | **pronto** — gates 01–04 verdes |
| M3 | NAT (`nat-to`/`rdr-to`/`binat-to`), scrub, pflog | **pronto** — gates 05/06/07/09 (incl. integridade de 2 MB via redirect) e pflog gate 12 |
| M4 | route-to/dup-to/reply-to, pools, divert e2e | **pronto** — gates 08/10/13 e divert gate 11 |
| M5 | filas: back ends ifq (hfsc/fq_codel) | **concluído (mecânico)** — gate 15: o hfsc é anexado, `pfctl -s queue -v` mostra as stats das classes, um fluxo de 2 MB pela fila é contado pela classe. `bandwidth` = linkshare do HFSC (garantia sob contenção, não teto) |
| M6 | docs completas, patchset limpo, suíte verde, releases com tag | **concluído** — docs EN+PT-BR, suíte 15/15 verde, tag `v1.0.0` |

## Próximas leituras

[build.md](build.md) → [vm-lab.md](vm-lab.md) → [pf-guide.md](pf-guide.md) →
[feature-matrix.md](feature-matrix.md) → [caveats.md](caveats.md).
