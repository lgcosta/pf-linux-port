# Matriz de features

English: [docs/en/feature-matrix.md](../en/feature-matrix.md).

Vocabulário de status (idêntico nos dois idiomas):

- **VERIFIED-in-VM** — passou de ponta a ponta na bancada libvirt (ver [vm-lab.md](vm-lab.md)).
- **M2-in-progress** — implementado; os gates VM 01–04 existem, mas a suíte **ainda não está toda verde**; sem alegação de VERIFIED até uma rodada limpa.
- **COMPILES-untested** — compila em `pf.ko`/`pfctl`; nenhum gate exercitou.
- **PENDING-Mx** — entra no milestone Mx; o comportamento observável hoje está declarado.
- **OUT-OF-SCOPE** — não-objetivo por projeto (spec §3).

Estado em 2026-11-11; evidência por linha: `out/test-logs/`,
`docs/porting-notes.md`, commits de milestone.

## Plano de controle e ferramentas

| Feature | Sintaxe de upstream | Milestone | Status |
|---|---|---|---|
| Carga do módulo, `/dev/pf` (0600) | `insmod pf.ko` | M1 | VERIFIED-in-VM |
| Habilitar/desabilitar | `pfctl -e`, `pfctl -d` | M1 | VERIFIED-in-VM |
| Carga de ruleset | `pfctl -f arquivo` | M2 | VERIFIED-in-VM (cargas bem-sucedidas em toda rodada capturada; o status geral da rodada é misto — ver [vm-lab.md](vm-lab.md) §6) |
| Checagem só de parse | `pfctl -n -f arquivo` | M1 | VERIFIED (suíte de fixtures offline, `make -C tools test`) |
| Status/contadores | `pfctl -s info [-v]` | M1 | VERIFIED-in-VM |
| Listagem de regras | `pfctl -s rules [-v]` | M2 | COMPILES-untested |
| Listagem de estados | `pfctl -s states [-v]` | M2 | M2-in-progress — o gate 01 mostra anomalia de listagem (ping passa, nenhum estado icmp exibido) enquanto a máquina de estados está sendo corrigida |
| Tabelas (listagem/ops) | `pfctl -s tables`, `-t t -T show/add/del/kill` | M2 | M2-in-progress (gate 03) |
| Anchors | `pfctl -a anchor -f arquivo`, `-s Anchors` | M2 | M2-in-progress (gate 04) |
| Listagem de filas | `pfctl -s queue -v`, `-s ifq -v` | M5 | PENDING-M5 (o parse funciona; contadores ainda não conectados aos back ends reais) |
| Paridade de ABI ioctl | `make -C tools abi` (tabelas do abitest) | M1 | VERIFIED no lado do build (tabela userland); o diff do lado kernel é um passo aberto para o integrador (`tools/UPSTREAM.md`) |

## Núcleo de filtragem

| Feature | Sintaxe de upstream | Milestone | Status |
|---|---|---|---|
| `pass`/`match`, quick, match por interface/proto/endereço/porta | `pass in quick on wan proto tcp from 10/8 to port 80` | M2 | M2-in-progress (o caminho de dados roda o `pf_test` real; rodada parcial recente: ping de trânsito OK, icmp bloqueado OK) |
| `block drop` | `block drop in quick …` | M2 | M2-in-progress (gate 02a) |
| `block return` / `return-rst` / `return-rst after quarter` | `block return-rst in quick proto tcp …` | M2 | M2-in-progress (gate 02b) |
| `keep state` (icmp/tcp/udp), tabela de estados, purger | `… keep state` | M2 | M2-in-progress |
| `set state-policy`, `allow-opts` etc. | opções `set …` | M2 | COMPILES-untested |
| Syncookies (caminho de estouro de estado) | `set syncookies always` | M2 | COMPILES-untested |
| Tabelas pfr (radix/hash) | `table <blk> persist { 1.2.3.4 }`, `-T add …` | M2 | M2-in-progress (gate 03) |
| Anchors + include + precedência de quick | `anchor "x"`, `load anchor …` | M2 | M2-in-progress (gate 04) |
| IPv6 para tudo acima | mesma sintaxe, v6 | M2+ | COMPILES-untested (o engine é completo para v6 por construção; gates v6 smoke planejados) |
| Tráfego não-IPv4/IPv6 (ARP…) | — | M2 | PFLINUX por construção: hooks netfilter só de IPv4/IPv6 — ARP nunca entra no caminho do PF (sem gate ainda) |

## NAT / normalização (M3)

| Feature | Sintaxe de upstream | Milestone | Status |
|---|---|---|---|
| `nat-to` (pools de portas, sticky-port, static-port) | `pass out … nat-to (eth1:0)` | M3 | PENDING-M3 (`pf.c`/`pf_lb.c` compilados; fiação + gate VM no M3) |
| `rdr-to` | `rdr pass on wan proto tcp from any to any port 8080 -> srv port 80` | M3 | PENDING-M3 |
| `binat-to` | `binat-to …` | M3 | PENDING-M3 |
| Scrub/normalize: remontagem, `fragment drop-overlap`, `min-ttl`, `max-mss`, `df`/`no-df`, `random-id`, `set reassemble yes` | `match on all scrub (max-mss 1380 …)` | M3 | COMPILES-untested (`pf_norm.c` compilado no pf.ko e alcançado pelo `pf_test`; gate scapy 08 pendente) |
| Fingerprint de SO em estados | `pass … set os A` + `/etc/pf.os` | bônus | COMPILES-untested (`pf_osfp.c` compilado; sem empacotamento de fingerprints ainda — ausência de `/etc/pf.os` é tolerada) |

## Roteamento / divert / filas / logging

| Feature | Sintaxe de upstream | Milestone | Status |
|---|---|---|---|
| Pools de `route-to` (`rr`, `round-robin`, `source-hash`, `hash`), `reply-to`, `dup-to`, `fail-to` | `pass out route-to (GW 10.0.3.2)` | M4 | PENDING-M4 (o glue Linux de rota/neigh `pf_route` ainda não existe) |
| `divert-to` / tag `divert` | `pass in … divert-to <host> port <n>` | VERIFICADO em lab | **Funciona ponta a ponta.** O módulo registra em runtime a família de sockets `AF_DIVERT` (`PF_DIVERT=32`) — sem patch no kernel. A regra `divert-to` entrega o pacote a essa família; sem listener o pacote é descartado (`divs_noport`, paridade BSD); com `tools/pdivert` rodando o pacote vai ao userspace, é reescrito e reinjetado (verificado com mutação de payload + correção de checksum, ping 0% loss). |
| Filas, agendador default (priq) | `queue root on wan`, `pfctl -s queue -v` | M5 | COMPILES-untested — `compat/pfk_ifq.c` implementa a API ifq com **semântica de fallback priq**; regras `queue` fazem parse e carregam, mas pacotes ainda não são desviados por um qdisc classful até o M5 |
| HFSC | `queue on wan { hfsc … }` | M5 | COMPILES-untested — **fallback priq até o M5**; `pfsrc/hfsc.c` staged (código real do OpenBSD 8.0) mas **fora da lista de objetos** do módulo |
| FQ-CoDel | `queue on wan { fq_codel … }` | M5 | COMPILES-untested — igual ao HFSC: staged, não compilado |
| Qdisc `pfq` (`tc qdisc … root pfq`) | n/a (glue Linux) | M5 | PENDING-M5 (ainda não existe na árvore) |
| pflog (`log`, `log all`, `block log`, `pfctl -s log`) + pcap do `pflogd` (DLT_PFLOG) | `pass out quick log` | M4 | PENDING-M4 — `pflog_packet()` retorna sucesso **sem logar** (`pr_warn "packet not logged"` uma única vez); `if_pflog.c` staged, não compilado; `pflogd` ainda não portado |

## Não-objetivos (explícitos)

| Item | Motivo |
|---|---|
| Failover `pfsync` / CARP | não-objetivo (spec §3) |
| IPsec / pfkey | não-objetivo |
| `ftp-proxy` e daemons de userland | não-objetivo |
| Instâncias PF por netns | port fiel = instância global única em `init_net` |
| Agendadores exclusivos do ALTQ clássico (cbq/wfq/red…) | removidos do upstream antes do OpenBSD 8.0 — impossível portar da base |
| Labels de rota (`route-label`) | não-objetivo; a camada de ioctl rejeita regras com label |
