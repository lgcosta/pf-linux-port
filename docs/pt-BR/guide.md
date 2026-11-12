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
| `module/pfsrc/` | Arquivos portados do OpenBSD: `pf.c` (engine), `pf_ioctl.c` (ABI DIOC*), `pf_norm.c` (scrub/fragmentos), `pf_table.c`, `pf_ruleset.c`, `pf_if.c`, `pf_lb.c` (pools/portas NAT), `pf_osfp.c`, `pf_syncookies.c`, `ifq.c`/`hfsc.c`/`fq_codel.c` (staged para M5), `if_pflog.c` + `ip_divert.c` (referências staged), `divert_sock.c` (stub de entrada do divert), `pfk_mod.c` (load/exit, `/dev/pf`), `pfk_hooks.c` (caminho de dados netfilter) |
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

- **Filas**: o `pfctl` faz o parse de `queue`/`queue on` (sintaxe 8.0); no
  kernel a API ifq é implementada por `compat/pfk_ifq.c` com **semântica
  priq apenas**; o qdisc classful `pfq` e os back ends HFSC/FQ-CoDel chegam
  em **M5**. `ifq.c`/`hfsc.c`/`fq_codel.c` estão staged e prontos para
  compilar, mas ainda **não** estão na lista de objetos do módulo.
- **Divert**: `divert-to`/tag `divert` produzem o veredito `PF_DIVERT` do PF;
  `divert_packet()` (`pfsrc/divert_sock.c`) hoje **descarta o pacote com um
  `pr_warn` rate-limited**, porque a família de sockets AF_DIVERT ainda não
  foi instanciada. O relay userspace `pdivert` existe e espera
  `socket(PF_DIVERT=32, SOCK_RAW, IPPROTO_DIVERT=254)`.
- **pflog**: `pflog_packet()` retorna sucesso mas **não loga nada** (um
  único `pr_warn "packet not logged"`); `if_pflog.c` está staged, back end
  netlink em M4/M5.

## Milestones (status por feature em feature-matrix.md)

| Milestone | Escopo | Status (2026-11-11) |
|---|---|---|
| M0 | repo, scripts, build do kernel, VM liga com kernel próprio | concluído |
| M1 | camada de compat, `pf.ko` carrega, `/dev/pf`, `pfctl -f`/`-s info` | concluído |
| M2 | núcleo de filtragem: gates VM de regras/estados/tabelas/anchors | **em andamento** — caminho de dados implementado, gates 01–04 ainda não todos verdes |
| M3 | NAT (nat/rdr/binat), scrub e2e, pflog | pendente |
| M4 | route-to/dup-to, divert e2e | pendente |
| M5 | filas: back ends ifq (hfsc/fq_codel) + qdisc `pfq` | pendente |
| M6 | docs bilíngues completas, patchset arrumado, suíte inteira verde, tags de release | em andamento (este conjunto de docs) |

## Próximas leituras

[build.md](build.md) → [vm-lab.md](vm-lab.md) → [pf-guide.md](pf-guide.md) →
[feature-matrix.md](feature-matrix.md) → [caveats.md](caveats.md).
