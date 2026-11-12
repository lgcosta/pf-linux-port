# Screencasts / Videos da demonstracao

> PT-BR: esta pagina · EN: [docs/en/screencast.md](en/screencast.md)

Seis videos silenciosos, 1920x1080 @ 30fps, H.264/MP4 — prontos para subir no
YouTube. Cada video e a gravacao de uma sessao **real** contra o lab: VMs
Alpine sob libvirt, rodando o kernel 7.3.4 compilado do fonte com o `pf.ko`
deste repositorio. Nada e simulado; a unica coisa roteirizada e o ritmo.

| # | Video | Duracao | O que mostra |
|---|-------|---------|--------------|
| 1 | `01-implementacao-kernel.mp4` | 22 s | A fonte do OpenBSD 8.0 organizada em `module/pfsrc`, a camada de compatibilidade em `module/compat`, e a compilacao do modulo `pf.ko` contra o kernel do lab |
| 2 | `02-ativacao.mp4` | 29 s | `insmod pf.ko`, `/dev/pf` aparecendo, `pfctl -e`, `pfctl -s info` com a tabela de estados |
| 3 | `03-regras-basicas.mp4` | 54 s | Um `pf.conf` real: tabela, anchor, `quick`, `keep state`, `block drop` (o ping morre), contadores por regra e `pfctl -ss` |
| 4 | `04-roteamento.mp4` | 47 s | `route-to` (o pacote sai pelo caminho forcado, provado com `tcpdump` nas duas interfaces) e `reply-to` (resposta por gateway diferente) |
| 5 | `05-balanceamento.mp4` | 69 s | Pool `nat-to { ... } round-robin` alternando a origem no fio, pool `source-hash` fixando por origem, e `dup-to` replicando o pacote |
| 6 | `06-qos.mp4` | 34 s | Filas do OpenBSD 8.0 (`queue ... bandwidth`, classes `hfsc`), `pfctl -s queue -v`, transferencia de 2 MB pela fila e os contadores da classe subindo |

## Como regravar

```sh
# renderiza todas as fases
./scripts/screencast/record-all.sh

# ou uma fase especifica
./scripts/screencast/record-all.sh 05-balanceamento
```

Os arquivos sao gravados em `out/screencast/*.mp4`. O renderizador
(`scripts/screencast/render.py`) roda a fase em um pty, emula o terminal com
`pyte`, converte cada evento em um frame PNG e mux com `ffmpeg` (H.264,
`yuv420p`, `+faststart`, **sem trilha de audio**). Se o `ffmpeg` da maquina nao
tiver `libx264`, ele cai para `libopenh264` e depois para VP9 automaticamente.

Requisitos: `ffmpeg`, `python3` com `PIL` e `pyte`, a fonte DejaVu Sans Mono, e
o lab de VMs no ar (as fases 2 a 6 falam com o lab; a fase 1 roda no host de
build).

## Roteiro das fases

Os roteiros estao em `scripts/screencast/phases/` — um shell por fase, com
comentarios explicando cada passo. Eles usam os mesmos comandos que os gates
da suite (`tests/vm/*.sh`), entao o que o video mostra e exatamente o que a
suite verifica.

## Texto de descricao (YouTube)

```
PF on Linux - o motor do OpenBSD PF 8.0 dentro do kernel Linux

Port completo do Packet Filter do OpenBSD 8.0 para o kernel Linux 7.3.4,
empacotado como modulo (pf.ko) e controlada pelo pfctl original do OpenBSD
sobre a ABI de ioctls do /dev/pf.

O lab e real: 4 VMs Alpine sob libvirt, kernel compilado do fonte com o
modulo, trafego real entre cliente, firewall e servidor.

Fases:
1. Implementacao no kernel - das fontes OpenBSD ao pf.ko
2. Ativacao - insmod, /dev/pf, pfctl -e, pfctl -s info
3. Regras basicas - tabelas, anchors, keep state, block drop
4. Roteamento - route-to e reply-to
5. Balanceamento - pools round-robin, source-hash e dup-to
6. QOS - filas hfsc do OpenBSD 8.0 no caminho de dados

Suite automatizada: 15 gates, todos verdes.
Codigo e documentacao: https://git.labexposed.com/lgcosta/pf-linux-port

#linux #kernel #openbsd #pf #firewall #netfilter #screencast
```
