# PF on Linux

Port of the OpenBSD **PF** (packet filter) to **Linux 7.3.4** as out-of-tree
kernel modules — the real PF engine (`pf.c`, `pf_norm.c`, `pf_table.c`,
`pf_lb.c`, … ported from OpenBSD 8.0 sources) driven by the real OpenBSD
`pfctl` over the `/dev/pf` ioctl ABI.

**Status:** implementation in progress — see
[`docs/superpowers/plans/2026-11-11-pf-linux-port.md`](docs/superpowers/plans/2026-11-11-pf-linux-port.md)
for task progress and
[`docs/superpowers/specs/2026-11-11-pf-linux-port-design.md`](docs/superpowers/specs/2026-11-11-pf-linux-port-design.md)
for the design.

## What you get (planned)

| Area | Feature |
|---|---|
| Filtering | rules, anchors, quick, `match`/`block`(drop/return/return-rst)/`pass`, `keep state`, syncookies |
| State/Tables | stateful firewall engine, `pfr` tables (radix + hash, `pfctl -t`) |
| NAT | `nat-to` (port pools, sticky-port, static-port), `rdr-to`, `binat-to` |
| Scrub | fragment reassembly, overlap drop, `min-ttl`/`max-mss`/`df`/`no-df`, `set reassemble` |
| QoS (ALTQ successor) | `queue`/`queue on` with OpenBSD 8.0 `ifq` schedulers: **default (priq), HFSC, FQ-CoDel**, attached via the `pfq` qdisc; `pfctl -s queue -v` stats |
| Divert | `divert-to` / `divert` tag + `pdivert` userspace channel (netlink; Linux has no `AF_DIVERT`) |
| Route | `route-to` (pools rr/hash-source/hash-state, `fail-to`), `reply-to`, `dup-to` |
| Logging | `pflogd` via netlink pflog channel (DLT_PFLOG pcap) |
| Control | `/dev/pf` (0600), OpenBSD 8.0 ioctl ABI, Linux build of `pfctl` (single binary) |

Not in scope: `pfsync`/CARP failover, IPsec/pfkey, `ftp-proxy`, per-netns PF
instances, classic-ALTQ-only schedulers (cbq/wfq/red — removed upstream before
OpenBSD 8.0).

## Quick start (after M1+)

```sh
./scripts/fetch-sources.sh          # OpenBSD 8.0 sources (C3SL mirror, checksummed)
./scripts/apply-patches.sh ../linux-7.3.4   # core export patch(es)
make module tools                   # pf.ko, pfq.ko, pfctl, pdivert, pflogd
sudo insmod module/pf.ko module/pfq.ko
sudo pfctl -f /etc/pf.conf && sudo pfctl -e
```

VM validation lab (Alpine on libvirt): see `docs/en/vm-lab.md`.
Documentação em português: [`README.pt-BR.md`](README.pt-BR.md) e `docs/pt-BR/`.

## Layout

- `module/` — kernel sources (ported PF + compat layer + pfq qdisc)
- `tools/` — pfctl/pdivert/pflogd Linux builds
- `patches/kernel/` — documented Linux core patches (export symbols only)
- `scripts/` — fetch/build/patch/VM automation
- `tests/` — VM suite (`tests/vm/NN-*.sh`, scapy helpers)
- `docs/en/` + `docs/pt-BR/` — full bilingual documentation
- `docs/superpowers/` — design spec + implementation plan

## License

Ported OpenBSD code retains its original [ISC license and copyright notices](LICENSE).
New Linux-side code: ISC.
