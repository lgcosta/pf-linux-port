# PF no Linux

Port do **PF** (packet filter) do OpenBSD para o **Linux 7.3.4** como módulos
out-of-tree — o engine PF real (`pf.c`, `pf_norm.c`, `pf_table.c`, `pf_lb.c`, …
portados dos fontes do OpenBSD 8.0) controlado pelo `pfctl` real do OpenBSD via
a ABI ioctl de `/dev/pf`.

**Status:** implementação em andamento — veja o
[plano de implementação](docs/superpowers/plans/2026-11-11-pf-linux-port.md) e o
[design doc](docs/superpowers/specs/2026-11-11-pf-linux-port-design.md).

## O que você obtém (planejado)

| Área | Recurso |
|---|---|
| Filtragem | regras, anchors, quick, `match`/`block`(drop/return/return-rst)/`pass`, `keep state`, syncookies |
| Estado/Tables | firewall stateful, tables `pfr` (radix + hash, `pfctl -t`) |
| NAT | `nat-to` (pools de portas, sticky-port, static-port), `rdr-to`, `binat-to` |
| Scrub | remonta­gmento de fragmentos, drop por overlap, `min-ttl`/`max-mss`/`df`/`no-df`, `set reassemble` |
| QoS (sucessor do ALTQ) | `queue`/`queue on` com os agendadores `ifq` do OpenBSD 8.0: **default (priq), HFSC, FQ-CoDel**, anexados via qdisc `pfq`; estatísticas `pfctl -s queue -v` |
| Divert | `divert-to` / tag `divert` + canal userspace `pdivert` (netlink; Linux não tem `AF_DIVERT`) |
| Roteamento | `route-to` (pools rr/hash-source/hash-state, `fail-to`), `reply-to`, `dup-to` |
| Logging | `pflogd` via canal netlink pflog (pcap DLT_PFLOG) |
| Controle | `/dev/pf` (0600), ABI ioctl do OpenBSD 8.0, `pfctl` compilado para Linux (binário único) |

Fora de escopo: failover `pfsync`/CARP, IPsec/pfkey, `ftp-proxy`, instâncias PF
por netns, agendadores exclusivos do ALTQ clássico (cbq/wfq/red — removidos do
OpenBSD antes da 8.0).

## Início rápido (após M1+)

```sh
./scripts/fetch-sources.sh          # fontes OpenBSD 8.0 (mirror C3SL, com checksum)
./scripts/apply-patches.sh ../linux-7.3.4   # patch(es) de core (exports)
make module tools                   # pf.ko, pfq.ko, pfctl, pdivert, pflogd
sudo insmod module/pf.ko module/pfq.ko
sudo pfctl -f /etc/pf.conf && sudo pfctl -e
```

Lab de validação em VMs (Alpine via libvirt): `docs/pt-BR/vm-lab.md`.
Documentation in English: [`README.md`](README.md) and `docs/en/`.

## Estrutura

- `module/` — fontes de kernel (PF portado + camada de compat + qdisc pfq)
- `tools/` — builds Linux de pfctl/pdivert/pflogd
- `patches/kernel/` — patches documentados no core do Linux (apenas exports)
- `scripts/` — automação de fetch/build/patch/VM
- `tests/` — suíte em VM (`tests/vm/NN-*.sh`, helpers scapy)
- `docs/en/` + `docs/pt-BR/` — documentação completa bilíngue
- `docs/superpowers/` — design + plano de implementação

## Licença

O código portado do OpenBSD mantém as licenças e avisos originais
([ISC](LICENSE)). Código novo do lado Linux: ISC.
