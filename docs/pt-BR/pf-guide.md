# Usando o PF-on-Linux no dia a dia

English: [docs/en/pf-guide.md](../en/pf-guide.md).

Este é o guia do "o que dá para rodar hoje". Ele segue a
[matriz de features](feature-matrix.md): o escopo do **M2 (núcleo de
filtragem: pass/block/quick/keep state/tabelas/anchors + habilitar/
desabilitar)** tem evidência em VM; tudo além disso é COMPILES-untested ou
PENDING. Instalação/build ficam em [install.md](install.md) e
[build.md](build.md).

## Ciclo diário

```sh
sudo insmod module/pf.ko          # cria /dev/pf
sudo pfctl -nf /etc/pf.conf       # checagem de parse (só userland — sempre segura)
sudo pfctl -f /etc/pf.conf        # carrega o ruleset
sudo pfctl -e                     # HABILITA — o tráfego passa a bater nas regras AGORA
sudo pfctl -s info                # status + contadores
sudo pfctl -s states              # listagem de estados (ver ressalva abaixo)
sudo pfctl -t blk -T add 10.0.1.10   # atualização de tabela ao vivo
sudo pfctl -a admin -f /etc/pf.d/admin.conf
sudo pfctl -d                     # desabilita
sudo rmmod pf                     # descarrega (regras/estados são liberados aqui)
```

> **Atenção — a ordem importa**: `-f` e depois `-e`. Habilitar sem uma regra `pass`
> que cubra o caminho de administração corta esse caminho — este port segue
> a política padrão de bloqueio do OpenBSD, e as rodadas do gate M2
> *realmente derrubaram o ssh da VM de teste* assim (a bancada mantém console
> serial exatamente por isso). Num host remoto, libere a interface de
> gerência antes de habilitar qualquer coisa.

## Um ruleset que funciona neste port hoje

```pf
# /etc/pf.conf — apenas escopo M2. Os nomes de interface aqui são os da
# bancada (mgmt/lan/wan); troque pelos seus (eth0, enp1s0, …).

# 1) mantenha você mesmo alcançável (faça isso primeiro, sempre)
pass in  quick on mgmt
pass out quick

# 2) núcleo de filtragem — comprovado hoje: quick, match de proto/porta, state, drop
table <blk> persist                       # blocklist viva (padrão do gate 03)
block in quick proto tcp from <blk> to any

pass in  quick on lan proto tcp from 10.0.1.0/24 to any port { 80 443 } keep state
block drop in quick on wan proto icmp from any to any

# 3) anchors com precedência (padrão do gate 04)
anchor "admin"
```

Macros (`xif = "lan"`), `anchor "in/*"` aninhado, `label`, `tag`,
`set timeout/limit/state-policy/block-policy/skip` e o resto da gramática do
OpenBSD 8.0 **fazem parse** sem problema — o `make -C tools test` exercita
tudo isso offline — mas o *comportamento no kernel* da maioria das opções
`set` ainda não foi verificado em gate; trate como "carrega e reza" até a
matriz mudar.

## O que evitar até os milestones chegarem

| Sintaxe | Até | O que acontece hoje |
|---|---|---|
| `nat-to`, `rdr-to`, `binat-to` | M3 | As regras carregam (código do engine compilado), mas a tradução não está fiada/verificada — **não conte com NAT**; no laboratório o masquerade de saída ainda vem do iptables/nft. |
| `scrub (…)`, `set reassemble` | M3 | O `pf_norm.c` roda dentro do `pf_test` (compilado) — o comportamento com fragmentos forjados não foi testado; trate o resultado como não validado. |
| `divert-to`, tag `divert` | M4 (+ família de sockets conforme comentário no código) | Os pacotes casados são **descartados** com um aviso de kernel rate-limited. Nunca coloque isso num host em produção. |
| `route-to`, `dup-to`, `fail-to`, `reply-to` | M4 | Ainda não implementados como glue de roteamento Linux; as regras podem carregar, mas o comportamento é indefinido — evite. |
| `queue` / `queue on` / `pfctl -s queue` | M5 | Faz parse e carrega. A fila cai no fallback priq da camada `ifq` de compat; **nenhuma limitação de banda acontece até o qdisc `pfq` chegar**. |
| `log`, `log all`, `block log`, `pflogd` | M4 | Nada é logado (o stub avisa uma vez no load: `pfk: … packet not logged`). |
| `route-label …` | nunca (não-objetivo) | Rejeitado na camada de ioctl. |
| política por container (netns) | fora de escopo | O PF é uma instância global em `init_net`; network namespaces **não** enxergam o PF. |

## Lendo o estado corretamente

O `pfctl -s states` tem hoje uma anomalia de listagem/visibilidade sendo
corrigida no trabalho do gate M2 (fluxos de trânsito podem passar enquanto
nenhum estado é exibido). Até lá, use contadores (`pfctl -s info`) e
observação externa (resultado do ping/nc, `tcpdump`) como evidência
primária, exatamente como os gates fazem.

## Sinais no log do kernel que você deve conhecer

```
pfk: PF-on-Linux engine loaded (OpenBSD 8.0 base)     ← insmod ok
pfk: netfilter hooks registered (10, netisr wq data path)
pfk: divert v4 port N: no divert socket layer (M6 pending), dropping
pfk: … packet not logged                              ← stub do pflog
```

Descarregar o módulo primeiro para os timers de purge e drena a workqueue
(um travamento de rmmod observado no lab foi corrigido assim) — mas a rodada
de *soak* (ciclo rmmod/insmod sob tráfego) ainda é um gate aberto, então
trate o unload como correto para uso administrativo e não comprovado para
automação de hot-reload.

Lista completa das divergências de comportamento: [caveats.md](caveats.md).
