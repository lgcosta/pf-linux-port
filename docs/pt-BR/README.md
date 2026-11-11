# Documentação PF-on-Linux (português)

English documentation: [`docs/en/README.md`](../en/README.md).

Este é o conjunto de documentação de usuário do port do **PF do OpenBSD 8.0**
para o **Linux 7.3.4** — o engine PF de verdade (`pf.c`, `pf_norm.c`,
`pf_table.c`, `pf_lb.c`, …) rodando como módulo out-of-tree `pf.ko`,
controlado pelo `pfctl` real do OpenBSD pela ABI ioctl de `/dev/pf`.

## Índice

| Documento | Conteúdo |
|---|---|
| [guide.md](guide.md) | Arquitetura em uma página: componentes, caminho de dados, plano de controle, milestones |
| [install.md](install.md) | Receita de aplicação/build/instalação (kernel, módulo, ferramentas) |
| [build.md](build.md) | Pipeline de build em detalhe: fetch → vendorize → kernel → módulo → pfctl |
| [vm-lab.md](vm-lab.md) | Bancada de validação em libvirt: topologia, recriar/rodar, o que cada gate prova |
| [feature-matrix.md](feature-matrix.md) | Status por feature: VERIFIED-in-VM / COMPILES-untested / PENDING-Mx |
| [pf-guide.md](pf-guide.md) | Uso diário: o que funciona hoje em `/etc/pf.conf`, o que evitar até M3/M4 |
| [patches.md](patches.md) | Patches de core do Linux (hoje vazio) + regras e template para futuros `000N-*.patch` |
| [caveats.md](caveats.md) | Divergências conhecidas de comportamento e limitações atuais |

## Fontes de verdade

A documentação descreve o código como ele existe na árvore. Em caso de dúvida,
as referências autoritativas são:

- `docs/superpowers/specs/2026-11-11-pf-linux-port-design.md` — design
- `docs/superpowers/plans/2026-11-11-pf-linux-port.md` — plano de milestones (tarefas T1–T19)
- `docs/porting-notes.md` — log de staging + registro de desvios da camada de compat (M1)

Vocabulário de status usado em todos os documentos:

- **VERIFIED-in-VM** — exercitado de ponta a ponta na bancada libvirt (ver vm-lab.md).
- **COMPILES-untested** — o código compila no `pf.ko`, mas nenhum gate provou o comportamento ainda.
- **PENDING-Mx** — entra (ou entra por completo) no milestone Mx; o comportamento observável hoje está documentado.
