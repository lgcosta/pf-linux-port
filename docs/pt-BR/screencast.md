# Screencasts / Vídeos da demonstração

> PT-BR: esta página · EN: [docs/en/screencast.md](../en/screencast.md)

Sete vídeos por idioma, com **narração de voz feminina** (MiniMax), **legendas
queimadas**, **diagrama de rede do lab** e um **filme completo** com transições
entre as fases. 1920×1080 @ 30fps, H.264/MP4 + AAC — prontos para subir no
YouTube.

Cada vídeo é a gravação de uma **sessão shell real**: uma conexão SSH de
verdade ao firewall Alpine, com os comandos digitados tecla a tecla e a saída
do kernel chegando em tempo real. Nada é simulado.

## As fases

| # | Vídeo | O que mostra |
|---|-------|--------------|
| 0 | `00-ambiente.mp4` | O lab: 4 VMs Alpine sob libvirt, as quatro interfaces do firewall e o diagrama de rede completo |
| 1 | `01-implementacao.mp4` | A implementação no kernel: fontes do OpenBSD 8.0 + camada de compatibilidade = `pf.ko` |
| 2 | `02-ativacao.mp4` | `insmod pf.ko`, `/dev/pf`, `pfctl -e`, `pfctl -s info` |
| 3 | `03-regras.mp4` | Regras básicas: tabela, anchor, `keep state`, `block drop` (o ping morre) |
| 4 | `04-roteamento.mp4` | `route-to` e `reply-to`, com o diagrama mostrando o caminho real do pacote |
| 5 | `05-balanceamento.mp4` | Pool `round-robin`, pool `source-hash` e `dup-to` |
| 6 | `06-qos.mp4` | Filas `hfsc` do OpenBSD 8.0 e uma transferência de 2 MB pela classe |

Mais `pf-on-linux-full.mp4`: o filme completo, com as sete fases emendadas por
transições (crossfade sobre um cartão de capítulo).

## Vozes MiniMax

| Idioma | Voz | Id |
|--------|-----|-----|
| Inglês (en_US) | Confident Woman | `English_ConfidentWoman` |
| Português (pt_BR) | Confident Woman | `Portuguese_ConfidentWoman` |

O áudio é gerado pelo MiniMax TTS, **um arquivo por segmento** de fala. Como
cada segmento corresponde a uma batida da sessão, a voz começa exatamente
quando a ação correspondente aparece na tela.

## Como regravar

```sh
# tudo: áudio -> sessões -> vídeos -> filme completo
./scripts/screencast/build-all.sh

# uma fase só
./scripts/screencast/build-all.sh 04-roteamento
```

Etapas individuais:

```sh
python3 scripts/screencast/make_audio.py --lang pt      # áudio MiniMax
python3 scripts/screencast/record.py --plan scripts/screencast/plans/04-roteamento.json \\
        --out out/screencast/caps/04-roteamento.cast --pace --ensure-pf
python3 scripts/screencast/assemble.py --phase 04-roteamento --lang pt
python3 scripts/screencast/join.py --lang pt
```

### Como o pipeline funciona

```
planos/*.json  --record.py-->  caps/*.cast  --render_video.py-->  vídeo mudo
   (comandos)      (pty real)    (asciicast)     (pyte + PIL)         |
                                                                    |
narracao/*.json --make_audio.py--> vo/<fase>.<lang>/*.mp3 ----------+
                          (MiniMax TTS)                                |
                                                  assemble.py: mix + legendas
                                                                    |
                                                          join.py: transições
```

Pontos que importam:

- **Sessão real.** `record.py` abre um pty, faz SSH na VM e digita os comandos
  um caractere por vez (~34 c/s, com jitter). O `bash --noediting` evita que o
  editor de linha redesenhe o comando e o tty o repita.
- **Pacing.** A gravação segura a tela enquanto a voz fala: cada batida dura no
  máximo o trecho de áudio que a acompanha (o maior entre EN e PT), para uma
  única captura servir as duas versões.
- **Legendas.** O texto da narração é queimado no vídeo, na língua da trilha.
- **Diagrama.** Nas fases 0, 4 e 5 o mapa do lab é desenhado sobre o terminal,
  e o caminho em discussão fica destacado (eth1/eth2/eth3).

Requisitos: `ffmpeg`, `python3` com `PIL` e `pyte`, a fonte DejaVu, a chave
`MINIMAX_API_KEY` no ambiente, e o lab de VMs no ar.

## Onde ficam os arquivos

| Caminho | Conteúdo |
|---------|----------|
| `out/screencast/en/`, `out/screencast/pt/` | os vídeos finais |
| `out/screencast/caps/` | as capturas asciicast das sessões |
| `out/screencast/vo/` | o áudio gerado pelo MiniMax |
| `scripts/screencast/plans/` | o roteiro de comandos de cada fase |
| `scripts/screencast/narration/` | os textos da narração (EN e PT) |

## Texto de descrição (YouTube)

```
PF on Linux - o motor do OpenBSD PF 8.0 dentro do kernel Linux

Port completo do Packet Filter do OpenBSD 8.0 para o kernel Linux 7.3.4,
empacotado como módulo (pf.ko) e controlado pelo pfctl original do OpenBSD
sobre a ABI de ioctls do /dev/pf.

O lab é real: 4 VMs Alpine sob libvirt, kernel compilado do fonte com o
módulo, tráfego real entre cliente, firewall e servidor.

Fases:
0. O ambiente de testes - as 4 VMs e o diagrama de rede
1. Implementação no kernel - das fontes OpenBSD ao pf.ko
2. Ativação - insmod, /dev/pf, pfctl -e, pfctl -s info
3. Regras básicas - tabelas, anchors, keep state, block drop
4. Roteamento - route-to e reply-to, com o caminho real no diagrama
5. Balanceamento - pools round-robin, source-hash e dup-to
6. QOS - filas hfsc do OpenBSD 8.0 no caminho de dados

Suíte automatizada: 15 gates, todos verdes.
Código e documentação: https://git.labexposed.com/lgcosta/pf-linux-port

#linux #kernel #openbsd #pf #firewall #netfilter #screencast
```
