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
- Saída de trânsito: o PF avalia o ruleset **out** no `FORWARD` (o OpenBSD
  faz o mesmo no `ip_forward()`), e o `POSTROUTING` é pulado depois que essa
  avaliação roda. `nat-to`/`rdr-to` foram validados contra essa ordem (gates
  05/06).
- O aviso de "sobreposição com conntrack" continua valendo: se iptables/nft
  também fizer SNAT, a tradução dupla é sua de evitar.

## Plano de controle / ciclo de vida

### 6. O unload está endurecido e verificado sob soak

O `rmmod pf` desregistra `/dev/pf`, zera `pf_status.running` sob a lock NET,
deleta **e sincroniza** os dois timers de purge (eles se rearmam sozinhos —
um travamento real de rmmod foi observado e corrigido parando as fontes
antes de drenar a workqueue), desregistra os hooks e drena o `pfk_netisr`.
A correção está na árvore, mas o ciclo completo sob tráfego + estados vivos
(gate de soak T18) **ainda não** rodou verde. Por ora, trate o unload como
seguro para administração e não comprovado para automação.

## Notas de comportamento verificadas hoje

### 7. `pfctl -s states` lista estados reais, incluindo traduções

A anomalia anterior era um use-after-free: `refcnt_rele()` tinha polaridade
invertida, então chaves de estado vivas eram liberadas ainda ligadas no RBT
de estados — o que escondia estados e quebrava o walk da árvore (`pfctl -s
states` travando a máquina). Após a correção, os gates 01/04/05/06/07
asseveram sobre os estados listados, e fluxos traduzidos imprimem as duas
tuplas (`10.0.2.1:59335 (10.0.1.10:4380) -> 10.0.2.20:8`).

### 8. Ainda são necessários consumidores

`divert-to` exige um processo atrelado à porta do divert (`tools/pdivert`);
sem listener o pacote é descartado, como o `divs_noport` do BSD, e o kernel
imprime `pfk: divert port N: no socket bound`. `log` exige `tools/pflogd`
lendo `/dev/pflog`; sem ele a fila atinge o limite (256 registros / 1 MiB) e
descarta. Nenhum dos dois é mais stub, mas nenhum faz algo sem o consumidor
rodar (ver [pf-guide.md](pf-guide.md)).

### 9. Configurar filas esbarra numa asserção de lock no caminho de purge (aberto)

Carregar uma regra `queue` agora alcança o escalonador (`pfq_alloc` tem sucesso,
a disciplina hfsc é criada) e `pf_commit_queues()` é exercitada de ponta a ponta.
Dois bugs foram encontrados e corrigidos no caminho: o handle da classe raiz
colidia com qids ímpares do pf (`pfq_addqueue` devolvia EBUSY) e a disciplina
NULL não era verificada.

O que ainda bloqueia as filas: o timer de purge do port pode rodar enquanto um
ioctl segura o lock de escrita, e `pf_purge_expired_states()` afirma o contrário:

```
splassert: pf_purge_expired_states: expected 0, got 2
kernel BUG at compat/pfk_kern.c:51
```

O OpenBSD serializa isso com `splnet()`; o port substituiu a camada spl por
rw-semáforos e work queue, então a asserção passou a ser uma corrida real em vez
de formalidade. Até corrigi-la (liberar o lock em torno da chamada, ou dar ao
estado o seu próprio lock como o upstream faz com `pf_state_lock`), um host que
configure filas pode travar o módulo em `Unloading`. **Não habilite regras
`queue` em host real ainda** — os 14 gates do lab nunca configuram filas, e é
por isso que continuam verdes.

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
