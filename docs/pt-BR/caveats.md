# Ressalvas — divergências do comportamento do OpenBSD

English: [docs/en/caveats.md](../en/caveats.md).

Estas são as formas pelas quais este port difere de rodar o mesmo ruleset no
OpenBSD 8.0, ou ainda não tem forma de produção. Tudo aqui tem base no
código (`module/pfsrc/pfk_hooks.c`, `module/pfsrc/pfk_mod.c`,
`docs/porting-notes.md`) e em observações da bancada; nada é especulação.

## Caminho de dados

### 1. O PF roda em contexto de processo via workqueue (modelo netisr), não no caminho rápido do pacote

O OpenBSD avalia o `pf_test()` na CPU que recebeu o pacote, em `IPL_NET`,
com uma lock de leitura NET que não dorme. O Linux nos dá contexto de
softirq e locks da compat *sleepáveis* (rw-semaphores), então o hook nunca
chama o `pf_test()` inline: ele furta a skb (`NF_STOLEN`) para uma
**workqueue ordenada** (`pfk_netisr`, `WQ_MEM_RECLAIM`), roda o engine em
contexto de processo e então reinjeta (`netif_rx()` na entrada,
`dst->output()` na saída/trânsito).

Consequências, todas observáveis:

- **Vereditos são assíncronos.** Qualquer regra encadeada depois do nosso
  hook na mesma chain netfilter (outras regras iptables/nft em prioridades
  maiores) não vê o pacote até o passe reinjetado chegar. Contadores e
  intercalação de log mudam na mesma medida.
- **Latência + vazão**: cada pacote paga um ida-e-volta pela workqueue,
  serializado num único worker ordenado. Os gates funcionais são a régua
  atual; o `bench-basic.sh` planejado (ainda não está na árvore) é só informativo.
- **Reordenação**: a fila ordenada preserva a ordem de chegada dos itens
  *enfileirados*, mas o stack em volta (RPS, multi-NAPI) ainda pode nos
  entregar pacotes cuja ordem original de conclusão difere da ordem de
  reinjeção.

### 2. A proteção contra reentrada é um tag por direção no `skb->mark`

O sobrevivente reinjetado carrega `skb->mark |= PFK_DONE_IN (0x50460010)` ou
`PFK_DONE_OUT (0x50460020)`; nossos hooks aceitam qualquer pacote já marcado
para aquela direção em vez de re-enfileirar (senão PF_IN → netif_rx → PF_IN…
entraria em loop). O `skb->mark` é um **campo compartilhado e gravável de
32 bits**: qualquer subsistema que o *sobrescreva* — `meta mark set` do nft,
`CONNMARK --restore`, filtros de tc, alguns stacks de VPN — pode (a) limpar o
nosso tag e fazer o pacote ser **re-enfileirado pelo PF de novo**, ou (b)
acidentalmente ligar esses bits e deixar o pacote **pular** um passe do PF.
Regra prática num host com PF-on-Linux: não use mangling de fwmark no mesmo
tráfego; se precisar, mascare os bits do tag do PF no seu espaço de mark.

### 3. Sem lock NET no caminho de dados (interino do M2)

O `pf_test()` roda hoje **sem** a lock de leitura NET da compat (comentário
em `pfk_hooks.c`): com o modelo de workqueue, um escritor de controle
parqueado (mudança de ruleset, ioctl de purge) esfomeava o lado dos pacotes
a ponto de ser contraproducente. A mutação do ruleset e a caminhada de
pacotes rodam, portanto, concorrentemente, protegidas pelo que o próprio
`pf.c` do upstream usa (PF_LOCK/padrões tipo RCU) — um desvio interino
deliberado, sendo retrabalhado junto com o modelo de locks no M5+. Até lá:
espere correção em regime estável, não sob reload concorrente agressivo; o
soak de reload sob tráfego é um gate aberto.

### 4. Fail-open sob pressão de memória

O item da fila é um `kmalloc(GFP_ATOMIC)` em softirq. Se falhar, o hook
retorna `NF_ACCEPT` — o pacote **passa por fora do PF**. É uma escolha
deliberada de disponibilidade (espelha "cai numa fila cheia = bypass do
qdisc"), mas é um trade-off de segurança que vale conhecer: os vereditos do
PF são best-effort exatamente quando o host está sem memória atômica.

### 5. A ordem dos hooks vs conntrack varia por direção

- Entrada (`PREROUTING`, `NF_IP_PRI_FIRST`): o PF roda **antes** do defrag
  e do conntrack — como projetado (spec §4.2).
- Saída (`LOCAL_OUT` em `NF_IP_PRI_CONNTRACK+1`): o PF vê o tráfego gerado
  localmente **depois** de o conntrack já ter confirmado a entrada.
- Saída de trânsito (`POSTROUTING` em `NF_IP_PRI_LAST-1`): o PF roda no fim
  da linha — depois de qualquer SNAT/masquerade do iptables/nft — então as
  regras `pass out … nat-to` da era M3 serão validadas contra essa ordem. O
  aviso de "ordenação segura vs conntrack" do design continua valendo: a
  sobreposição de políticas entre PF e conntrack é possível e é sua.

## Plano de controle / ciclo de vida

### 6. O unload está endurecido; o soak ainda está pendente

O `rmmod pf` desregistra `/dev/pf`, zera `pf_status.running` sob a lock NET,
deleta **e sincroniza** os dois timers de purge (eles se rearmam sozinhos —
um travamento real de rmmod foi observado e corrigido parando as fontes
antes de drenar a workqueue), desregistra os hooks e drena o `pfk_netisr`.
A correção está na árvore, mas o ciclo completo sob tráfego + estados vivos
(gate de soak T18) **ainda não** rodou verde. Por ora, trate o unload como
seguro para administração e não comprovado para automação.

## Problemas de comportamento/renderização abertos hoje

### 7. Anomalia de visibilidade do `pfctl -s states` (trabalho do gate M2)

Na rodada capturada mais recente (gate 01): o ping de trânsito **passou** com
`keep state`, mas nenhum estado icmp foi listado depois. Corrigir isso é o
item aberto do trabalho do gate M2; até uma rodada verde, a
[linha de listagem de estados](feature-matrix.md) é o status honesto. Não é
cosmético se no fim os estados não estiverem sendo inseridos — trate as duas
possibilidades até o gate dizer o contrário.

### 8. Stubs silenciosos-mas-avisados

Vereditos `divert` descartam com aviso rate-limited; regras de `log` não
logam nada e avisam uma única vez. Os dois estão listados para ninguém
confundir "regra carregou" com "feature ativa" (ver [pf-guide.md](pf-guide.md)).

## Desvios da camada de compat (de `docs/porting-notes.md`, registro do M1)

Estes são desvios deliberados e marcados no código; importam ao raciocinar
sobre a semântica das regras:

- **Match pelo dono do socket** (`user`/`pid` em regras): só uid — o
  `struct sock` do Linux não guarda egid/cpid, então o gid degrada para
  `(gid_t)-1` e o pid para `NO_PID`.
- **Checksums**: o offload do driver não é confiável na entrada (decisão do
  M1) — checksums TCP são recalculados em software; os pseudo-checksums de
  saída são feitos no caminho próprio do port.
- **Opções IP / record-route / timestamps**: não processadas no caminho de
  saída do PF; **redirecionamentos ICMP não são gerados** pelo código de
  rota do PF (o FIB do Linux emite).
- **Labels de rota**: não-objetivo — a camada de ioctl rejeita regras com
  label; `rtable_iterate()` sempre retorna NULL (sem iteração de uRPF
  multipath).
- **Grupos de interface**: o Linux não tem; matches `group …` se comportam
  como um grupo inexistente no OpenBSD.
- **Skbs GSO/multi-segmento**: tratadas por linearização na entrada do shim
  mbuf (custo de performance documentado no spec §10; os gates vão
  quantificar).
- **Instância única**: um mundo PF em `init_net`, como o OpenBSD tem um
  kernel. Containers/namespaces não são visíveis ao PF e não podem ser
  filtrados por ele neste design.

## O que *não* é ressalva

ARP e outros EtherTypes não-IPv4/IPv6 nunca entram nos hooks netfilter de
IPv4/IPv6 — passam pela máquina intactos, exatamente como o PF no OpenBSD
ignora não-IP. O `/dev/pf` é 0600 somente-root por projeto (equivalente ao
padrão do `pfctl` no OpenBSD da era `securelevel`).
