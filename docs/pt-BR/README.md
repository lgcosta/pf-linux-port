# Documentação do PF-on-Linux (português)

English documentation: [`docs/en/README.md`](../en/README.md).

Este é o conjunto de documentação de usuário do port do **packet filter do
OpenBSD 8.0 (PF)** para o **Linux 7.3.4** — o engine PF real (`pf.c`,
`pf_norm.c`, `pf_table.c`, `pf_lb.c`, `hfsc.c`, …) rodando como o módulo
out-of-tree `pf.ko`, controlado pelo `pfctl` real do OpenBSD sobre a ABI ioctl
de `/dev/pf`.

## Índice

| Doc | O que cobre |
|---|---|
| [guide.md](guide.md) | Arquitetura em uma página: componentes, caminho de dados, plano de controle, milestones |
| [install.md](install.md) | Receita de apply/build/install (kernel, módulo, ferramentas) |
| [debian13.md](debian13.md) | Implementação completa do zero no Debian 13 (trixie): preparar host, compilar kernel/módulo/ferramentas, lab libvirt, teste de stress |
| [build.md](build.md) | Pipeline de build em detalhe: fetch → vendorize → kernel → módulo → pfctl |
| [vm-lab.md](vm-lab.md) | O lab de validação em libvirt: topologia, recriar/rodar, o que cada um dos 16 gates prova |
| [feature-matrix.md](feature-matrix.md) | Status por feature: VERIFIED-in-VM / VERIFIED-offline / COMPILES-untested / NOT-ENABLED |
| [pf-guide.md](pf-guide.md) | Uso diário: o que funciona hoje em `/etc/pf.conf` e as divergências a conhecer |
| [screencast.md](screencast.md) | Os vídeos narrados (inglês e português): fases, pipeline, como regravar |
| [patches.md](patches.md) | Conjunto de patches de core do Linux (hoje vazio) + regras e template para futuros `000N-*.patch` |
| [caveats.md](caveats.md) | Divergências conhecidas em relação ao OpenBSD e limitações atuais |

## Fontes da verdade

Os docs descrevem o código como ele existe nesta árvore. Na dúvida, as
referências autoritativas são:

- `docs/superpowers/specs/2026-11-11-pf-linux-port-design.md` — design
- `docs/superpowers/plans/2026-11-11-pf-linux-port.md` — plano dos milestones (tarefas T1–T19)
- `docs/porting-notes.md` — log de staging + registro das divergências (a verdade sobre as divergências da camada de compat)

Vocabulário de status usado nestes docs:

- **VERIFIED-in-VM** — exercitado de ponta a ponta no lab libvirt; o gate que prova está nomeado (veja [vm-lab.md](vm-lab.md)).
- **VERIFIED-offline** — provado por um teste de fixture ou de ABI, não por tráfego.
- **COMPILES-untested** — o código é compilado dentro do `pf.ko`/`pfctl` mas nenhum gate o exercitou.
- **NOT-ENABLED** — o código existe mas a feature está desligada neste build.
- **OUT-OF-SCOPE** — fora de escopo por desenho (spec §3).

A suíte do lab está **16/16 verde** (`./scripts/vm-run-tests.sh`).
