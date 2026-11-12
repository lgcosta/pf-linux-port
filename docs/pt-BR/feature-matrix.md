# Matriz de features

Inglês: [docs/en/feature-matrix.md](../en/feature-matrix.md).

Vocabulário de status (idêntico nos dois idiomas):

- **VERIFIED-in-VM** — exercitado de ponta a ponta no lab libvirt (veja [vm-lab.md](vm-lab.md)); o gate que prova está nomeado.
- **VERIFIED-offline** — provado por um teste de fixture/ABI, não por tráfego (ex.: `make -C tools test`).
- **COMPILES-untested** — compilado dentro do `pf.ko`/`pfctl`; nenhum gate exercitou ainda.
- **NOT-ENABLED** — o código existe mas a feature está desligada neste build (ex.: `af-to`, que depende dos caminhos `INET6`).
- **OUT-OF-SCOPE** — fora de escopo por desenho (spec §3).

Atualizado em 2026-11-12; fonte da evidência por linha: `out/test-logs/`,
`docs/porting-notes.md`, commits dos milestones e os screencasts
([screencast.md](screencast.md)) para a prova visível.

A suíte está **16/16 verde** (`./scripts/vm-run-tests.sh`).

## Plano de controle e ferramentas

| Feature | Sintaxe upstream | Milestone | Status |
|---|---|---|---|
| Carga do módulo, `/dev/pf` (0600) | `insmod pf.ko` | M1 | VERIFIED-in-VM (gate `02-ativacao.sh`) |
| Habilitar/desabilitar | `pfctl -e`, `pfctl -d` | M1 | VERIFIED-in-VM |
| Carga de ruleset | `pfctl -f file` | M2 | VERIFIED-in-VM (todo gate carrega um ruleset real) |
| Checagem só de parse | `pfctl -n -f file` | M1 | VERIFIED-offline (`make -C tools test`) |
| Status/contadores | `pfctl -s info [-v]` | M1 | VERIFIED-in-VM |
| Listagem de regras | `pfctl -s rules [-v]`, `pfctl -sr` | M2 | VERIFIED-in-VM (gates 02–06) |
| Listagem de estados | `pfctl -s states [-v]`, `pfctl -ss` | M2 | VERIFIED-in-VM (gate `01-state.sh`) |
| Listagem/operações de tabelas | `pfctl -s tables`, `-t t -T show/add/del/kill` | M2 | VERIFIED-in-VM (gate `03-tables.sh`, incluindo um `-T delete` ao vivo liberando o fluxo) |
| Anchors | `pfctl -a anchor -f file`, `-s anchors` | M2 | VERIFIED-in-VM (gate `04-anchors.sh`) |
| Listagem de filas | `pfctl -s queue -v` | M5 | VERIFIED-in-VM (gate `15-queue.sh`: as duas classes com estatísticas) |
| Paridade da ABI ioctl | `make -C tools abi` (tabelas do abitest) | M1 | VERIFIED-offline (tabela do userland); o diff do lado do kernel é um passo de integração em aberto (`tools/UPSTREAM.md`) |

## Núcleo de filtragem

| Feature | Sintaxe upstream | Milestone | Status |
|---|---|---|---|
| `pass`/`match`, quick, on-interface/proto/addr/port | `pass in quick on wan proto tcp from 10/8 to port 80` | M2 | VERIFIED-in-VM (gate `01-state.sh`: ping de trânsito atravessando o firewall) |
| `block drop` | `block drop in quick …` | M2 | VERIFIED-in-VM (gate `02-block.sh`: o ping morre e os contadores provam) |
| `block return` / `return-rst` / `return-rst after quarter` | `block return-rst in quick proto tcp …` | M2 | VERIFIED-in-VM (gate `02-block.sh`: o `nc` do cliente recebe RST) |
| `keep state` (icmp/tcp/udp), tabela de estados, purger | `… keep state` | M2 | VERIFIED-in-VM (gate `01-state.sh`; o estado aparece no `pfctl -ss`) |
| `set state-policy`, `allow-opts`, etc. | opções `set …` | M2 | COMPILES-untested |
| Syncookies (caminho de overflow de estados) | `set syncookies always` | M2 | COMPILES-untested |
| Tabelas pfr (radix/hash) | `table <blk> persist { 1.2.3.4 }`, `-T add …` | M2 | VERIFIED-in-VM (gate `03-tables.sh`) |
| Anchors + include + precedência de quick | `anchor "x"`, `load anchor …` | M2 | VERIFIED-in-VM (gate `04-anchors.sh`) |
| IPv6 para tudo acima | mesma sintaxe, v6 | M2+ | COMPILES-untested — o engine é completo para v6 por construção, mas o caminho v6 não é exercitado por gate |
| Tráfego não-IPv4/IPv6 (ARP…) | — | M2 | PFLINUX por construção: só os hooks netfilter IPv4/IPv6 — ARP nunca entra no caminho do PF (sem gate) |

## NAT / normalização (M3)

| Feature | Sintaxe upstream | Milestone | Status |
|---|---|---|---|
| `nat-to` (pools de portas, sticky-port, static-port) | `pass out … nat-to (eth1:0)` | M3 | **VERIFIED-in-VM** — gate `05-nat.sh`: o NAT de origem funciona de ponta a ponta (prova por tcpdump), a resposta é des-NATeada e re-encaminhada para o cliente, e o estado mostra as duas tuplas (`10.0.2.1:59335 (10.0.1.10:4380) -> 10.0.2.20:8`). |
| `rdr-to` | `pass in … to <fw> port 8080 rdr-to <srv> port 80 keep state` | M3 | **VERIFIED-in-VM** — gate `06-rdr.sh`: o cliente fala com o endereço do firewall, o pf reescreve o destino, re-encaminha o pacote, e a resposta é des-redirecionada. O gate também puxa 2 MB pelo redirect e confere a integridade byte a byte (guarda de regressão GSO/TSO). |
| `binat-to` | `pass quick proto icmp from 10.0.2.20 to 10.0.1.10 binat-to 10.0.2.50 keep state` | M3 | **VERIFIED-in-VM** — gate `07-binat.sh`. O pfctl expande uma regra `binat-to` no par documentado (`pass out … nat-to <mapped> static-port` + `pass in … rdr-to <real>`), e as duas metades funcionam: o par vê o endereço mapeado, o original nunca vaza, e a resposta é traduzida de volta. |
| Pools de NAT (`round-robin`, `source-hash`) | `nat-to { 10.0.2.1, 10.0.3.1 } round-robin` | M4 | **VERIFIED-in-VM** — gate `10-pools.sh`, e visível no screencast: o round-robin alterna o endereço de origem entre os membros (3 pacotes cada), o source-hash mantém uma origem em um membro só. |
| Scrub/normalize: remontagem, `fragment drop-overlap`, `min-ttl`, `max-mss`, `df`/`no-df`, `random-id`, `set reassemble yes` | `match in all scrub (min-ttl 100 max-mss 500)` | M3 | **VERIFIED-in-VM** — gate `09-scrub.sh`: o `min-ttl` aumenta o TTL (egresso mostra 99 depois do decremento do forward) e o `max-mss` limita a opção MSS do SYN, ambos medidos no egresso do próprio firewall. Os caminhos de remontagem/overlap compilam e são alcançados, mas não têm gate. |
| Fingerprint de OS nos estados | `pass … set os A` + `/etc/pf.os` | bonus | COMPILES-untested (`pf_osfp.c` compilado; ainda sem o pacote de fingerprints — a ausência de `/etc/pf.os` é tolerada) |

## Roteamento / divert / filas / logging

| Feature | Sintaxe upstream | Milestone | Status |
|---|---|---|---|
| `route-to` / `dup-to` / `reply-to` | `pass in … route-to { 10.0.3.2 } keep state`, `reply-to { gw }` | M4 | **VERIFIED-in-VM** — gates `08-routeto.sh` (o route-to sai pelo link do gateway, o dup-to copia mantendo o fluxo) e `13-replyto.sh` (a resposta de um fluxo é forçada para o link do gateway em vez do cliente). Divergência: o próximo salto forçado é instalado definindo `rt_gw4`/`rt_gw_family` na rota consultada (o Linux só faz ARP do gateway quando a rota declara um). `fail-to` **não é palavra-chave no OpenBSD 8.0** (removido upstream), logo está fora de escopo. |
| `divert-to` / tag `divert` | `pass in … divert-to <host> port <n>` | M4 | **VERIFIED-in-VM** — gate `11-divert.sh`. O módulo registra uma família de sockets `AF_DIVERT` (`PF_DIVERT=32`) em runtime — sem patch no kernel. Uma regra `divert-to` entrega o pacote a essa família; sem listener o pacote é descartado (`divs_noport`, paridade com o BSD); com o `tools/pdivert` rodando o pacote vai para o userspace, é reescrito e reinjetado (verificado com mutação do payload + correção de checksum, ping com 0% de perda). |
| Filas (`queue`, `queue on`, `pfctl -s queue -v`) | `queue rootq on eth2 bandwidth 10Mbit` + `queue leafq on eth2 bandwidth 2Mbit parent rootq default` | M5 | **VERIFIED-in-VM** — gate `15-queue.sh`: a configuração de filas carrega, a disciplina hfsc é anexada à interface, `pfctl -s queue -v` lista as duas classes com estatísticas, um fluxo de 2 MB pela fila completa, e o contador de pacotes da classe avança (o escalonador está no caminho de dados). Note que `bandwidth` é *linkshare* do HFSC — garantia sob contenção, não teto: o pf.conf do OpenBSD 8.0 não tem `minimum`/`maximum`, então um fluxo isolado não é limitado, nem no Linux nem no OpenBSD. |
| HFSC | `queue on wan { hfsc … }` | M5 | **VERIFIED-in-VM** — o `pfsrc/hfsc.c` (código real do OpenBSD 8.0) está na lista de objetos do módulo e é a disciplina que o gate exercita. As classes usam o `net/hfsc.h` vendorizado (classe raiz `0x10000`, classe default `0x1000`) compartilhado com o `pfctl`, então a ABI de estatísticas bate nos dois lados. |
| FQ-CoDel | `queue on wan { fq_codel … }` | M5 | COMPILES-untested — o `pfsrc/fq_codel.c` é compilado dentro do `pf.ko` e o `net/fq_codel.h` vendorizado é compartilhado com o `pfctl`, mas nenhum gate o exercita. |
| Interface `pfq` | n/a (interfaces BSD `ifq`/`pfq`, não um qdisc `tc` do Linux) | M5 | **VERIFIED-in-VM** — o port mantém a forma do BSD: `compat/pfk_ifq.c` implementa as interfaces `ifq_ops`/`pfq_ops` e os back ends do OpenBSD se plugam nelas (`pfq` nunca foi um qdisc `tc` do Linux, nem nunca pretendeu ser). priq é a disciplina default. |
| pflog (`log`, `log all`, `block log`) + pcap do `pflogd` (DLT_PFLOG) | `pass out quick log` + `pflogd /tmp/x.pcap` | M4 | **VERIFIED-in-VM** — gate `12-pflog.sh`: os pacotes logados são enfileirados pelo engine, lidos por `/dev/pflog` e escritos como pcap com linktype DLT_PFLOG (117); o dump contém os bytes reais do pacote. Divergência: um chardev em vez de uma interface if_pflog/bpf (o Linux não tem bpf sobre uma interface virtual dessas aqui); o layout do registro é byte a byte compatível com o `pflog(4)` do OpenBSD. O `if_pflog.c` continua fora da build. |

## Fora de escopo (explícito)

| Item | Motivo |
|---|---|
| Failover `pfsync` / CARP | fora de escopo (spec §3) |
| IPsec / pfkey | fora de escopo |
| `ftp-proxy` e daemons de userland | fora de escopo |
| Instâncias PF por netns | port fiel = uma instância global no `init_net` |
| Agendadores exclusivos do ALTQ clássico (cbq/wfq/red…) | removidos upstream antes do OpenBSD 8.0 — impossíveis de portar a partir da base |
| Routing labels (`route-label`) | fora de escopo; a camada de ioctl rejeita regras com label |
| `af-to` (tradução IPv4↔IPv6) | o código está sob `#ifdef INET6` e este build tem `INET6 0`; habilitá-lo é um milestone à parte, não uma feature faltando na base |
