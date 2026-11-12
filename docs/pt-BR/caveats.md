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

### 9. Filas: caminho verificado; a semântica de modelagem é do HFSC, não um bug

`15-queue.sh` prova que o caminho de filas funciona de ponta a ponta: a
configuração carrega, a disciplina hfsc é anexada à interface, `pfctl -s
queue -v` lista as duas classes com estatísticas, um fluxo de 2 MB pela fila
completa, e o contador de pacotes da classe avança - o escalonador está mesmo
no caminho de dados.

O que o lab não mostra é um *teto de taxa*, e isso é semântica do HFSC, não
defeito do port: o `pf.conf` do OpenBSD 8.0 expõe apenas `bandwidth`, que mapeia
para a curva **linkshare** do HFSC - garantia quando classes competem, não limite
de um fluxo isolado. Não existe palavra-chave `minimum`/`maximum` nesta versão do
pfctl (verificado na sua tabela de palavras-chave), logo não há como definir
upperlimit pelo pf.conf. Um teste de contenção com duas classes saturadas seria o
próximo passo se você quiser asserir sobre share; no enlace virtual de centenas de
Mbit do lab, um único fluxo termina antes de acumular dívida de tempo virtual.

Bugs encontrados e corrigidos no caminho (detalhes em `docs/porting-notes.md`): a
convenção de retorno de `ifqop_enq`, `ifq_q` vs `ifq_softc` (deref de NULL que
matava a máquina no meio da transferência), o cookie de dequeue perdido, o
handle da classe raiz do hfsc colidindo com qids ímpares do pf, o
`DIOCGETQSTATS` falhando porque kernel e pfctl discordavam sobre
`struct hfsc_class_stats` (ambos agora usam o `net/hfsc.h` vendorizado), o
payload do pflog começando no cabeçalho de transporte, e o `splassert` do caminho
de purge que transformava uma corrida benigna em `BUG()`.

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

## Pipeline dos screencasts (2026-11-12)

### 10. A geração de vídeo do MiniMax não está habilitada nesta conta

Todos os modelos de vídeo do MiniMax (`T2V-01`, `T2V-01-Director`,
`MiniMax-Hailuo-02`) respondem com o erro de API **2068 — "The current Token
Plan tier does not support this model"**. A narração (TTS) e a arte do
cartão de título funcionam normalmente; só os modelos de vídeo estão
bloqueados. Por isso as transições entre fases são renderizadas localmente
com ffmpeg (um crossfade sobre um cartão de capítulo) em vez de clipes
gerados por IA. Mudar isso exige só subir o tier da conta — o ponto de
extensão é `scripts/screencast/join.py`, que constrói os cartões.

### 11. A narração é uma trilha separada, não uma dublagem do vídeo

Cada fase é gravada **uma vez** e serve aos dois idiomas: a sessão de
terminal é neutra em relação ao idioma, e as duas versões narradas diferem
apenas na trilha de áudio e nas legendas queimadas. O gravador segura cada
batida na tela pelo tempo que o segmento de narração mais longo (EN ou PT)
precisa, então a voz e a ação no terminal ficam em sincronia nos dois.

### 12. O que os vídeos não mostram

Os vídeos são uma demonstração, não uma rodada de teste: a suíte de gates do
lab (`scripts/vm-run-tests.sh`) é a autoridade para PASS/FAIL, e os logs
dela ficam em `out/test-logs/`. Os screencasts mostram os mesmos comandos,
mas um quadro de vídeo não é um veredito.

## O que *não* é ressalva

ARP e outros EtherTypes não-IPv4/IPv6 nunca entram nos hooks netfilter de
IPv4/IPv6 — passam pela máquina intactos, exatamente como o PF no OpenBSD
ignora não-IP. O `/dev/pf` é 0600 somente-root por projeto (equivalente ao
padrão do `pfctl` no OpenBSD da era `securelevel`).
