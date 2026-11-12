# Screencasts / Videos da demonstracao

> PT-BR: [docs/pt-BR/screencast.md](../pt-BR/screencast.md) · EN: this page

Sete videos por idioma, com **narracao com voz feminina** (MiniMax),
**legendas queimadas**, **diagrama de rede do lab** e um **filme completo**
com transicoes entre as fases. 1920x1080 @ 30fps, H.264/MP4 + AAC — prontos
para subir no YouTube.

Cada video e a gravacao de uma **sessao shell real**: uma conexao SSH de
verdade ao firewall Alpine, com os comandos digitados tecla a tecla e a saida
do kernel chegando em tempo real. Nada e simulado.

## As fases

| # | Video | O que mostra |
|---|-------|--------------|
| 0 | `00-ambiente.mp4` | O lab: 4 VMs Alpine sob libvirt, as quatro interfaces do firewall, e o diagrama de rede completo |
| 1 | `01-implementacao.mp4` | A implementacao no kernel: fontes do OpenBSD 8.0 + camada de compatibilidade = `pf.ko` |
| 2 | `02-ativacao.mp4` | `insmod pf.ko`, `/dev/pf`, `pfctl -e`, `pfctl -s info` |
| 3 | `03-regras.mp4` | Regras basicas: tabela, anchor, `keep state`, `block drop` (o ping morre) |
| 4 | `04-roteamento.mp4` | `route-to` e `reply-to`, com o diagrama mostrando o caminho real do pacote |
| 5 | `05-balanceamento.mp4` | Pool `round-robin`, pool `source-hash` e `dup-to` |
| 6 | `06-qos.mp4` | Filas `hfsc` do OpenBSD 8.0 e uma transferencia de 2 MB pela classe |

Mais `pf-on-linux-full.mp4`: o filme completo, com as sete fases emendadas por
transicoes (crossfade sobre um cartao de capitulo).

## Vozes MiniMax

| Idioma | Voz | Id |
|--------|-----|-----|
| Ingles (en_US) | Confident Woman | `English_ConfidentWoman` |
| Portugues (pt_BR) | Confident Woman | `Portuguese_ConfidentWoman` |

A audio e gerada pelo MiniMax TTS, **um arquivo por segmento** de fala. Como
cada segmento corresponde a uma batida da sessao, a voz comeca exatamente
quando a acao correspondente aparece na tela.

## Como regravar

```sh
# tudo: audio -> sessoes -> videos -> filme completo
./scripts/screencast/build-all.sh

# uma fase so
./scripts/screencast/build-all.sh 04-roteamento
```

Etapas individuais:

```sh
python3 scripts/screencast/make_audio.py --lang pt      # audio MiniMax
python3 scripts/screencast/record.py --plan scripts/screencast/plans/04-roteamento.json \\
        --out out/screencast/caps/04-roteamento.cast --pace --ensure-pf
python3 scripts/screencast/assemble.py --phase 04-roteamento --lang pt
python3 scripts/screencast/join.py --lang pt
```

### Como o pipeline funciona

```
planos/*.json  --record.py-->  caps/*.cast  --render_video.py-->  video mudo
   (comandos)      (pty real)    (asciicast)     (pyte + PIL)         |
                                                                    |
narracao/*.json --make_audio.py--> vo/<fase>.<lang>/*.mp3 ----------+
                          (MiniMax TTS)                                |
                                                  assemble.py: mix + legendas
                                                                    |
                                                          join.py: transicoes
```

Pontos que importam:

* **Sessao real.** `record.py` abre um pty, faz SSH na VM e digita os comandos
  um caractere por vez (~34 c/s, com jitter). O `bash --noediting` evita que o
  editor de linha redesenhe o comando e o tty o repita.
* **Pacing.** A gravacao segura a tela enquanto a voz fala: cada batida dura
  no maximo o trecho de audio que a acompanha (o maior entre EN e PT), para
  uma unica captura servir as duas versoes.
* **Legendas.** O texto da narracao e queimado no video, na lingua da trilha.
* **Diagrama.** Nas fases 0, 4 e 5 o mapa do lab e desenhado sobre o
  terminal, e o caminho em discussao fica destacado (eth1/eth2/eth3).

Requisitos: `ffmpeg`, `python3` com `PIL` e `pyte`, a fonte DejaVu, a chave
`MINIMAX_API_KEY` no ambiente, e o lab de VMs no ar.

## Onde ficam os arquivos

| Caminho | Conteudo |
|---------|----------|
| `out/screencast/en/`, `out/screencast/pt/` | os videos finais |
| `out/screencast/caps/` | as capturas asciicast das sessoes |
| `out/screencast/vo/` | o audio gerado pelo MiniMax |
| `scripts/screencast/plans/` | o roteiro de comandos de cada fase |
| `scripts/screencast/narration/` | os textos da narracao (EN e PT) |

## Texto de descricao (YouTube)

```
PF on Linux - o motor do OpenBSD PF 8.0 dentro do kernel Linux

Port completo do Packet Filter do OpenBSD 8.0 para o kernel Linux 7.3.4,
empacotado como modulo (pf.ko) e controlado pelo pfctl original do OpenBSD
sobre a ABI de ioctls do /dev/pf.

O lab e real: 4 VMs Alpine sob libvirt, kernel compilado do fonte com o
modulo, trafego real entre cliente, firewall e servidor.

Fases:
0. O ambiente de testes - as 4 VMs e o diagrama de rede
1. Implementacao no kernel - das fontes OpenBSD ao pf.ko
2. Ativacao - insmod, /dev/pf, pfctl -e, pfctl -s info
3. Regras basicas - tabelas, anchors, keep state, block drop
4. Roteamento - route-to e reply-to, com o caminho real no diagrama
5. Balanceamento - pools round-robin, source-hash e dup-to
6. QOS - filas hfsc do OpenBSD 8.0 no caminho de dados

Suite automatizada: 15 gates, todos verdes.
Codigo e documentacao: https://git.labexposed.com/lgcosta/pf-linux-port

#linux #kernel #openbsd #pf #firewall #netfilter #screencast
```
