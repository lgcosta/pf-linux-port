# PF on Linux

Port of the OpenBSD **PF** (packet filter) to **Linux 7.3.4** as an
out-of-tree kernel module — the real PF engine (`pf.c`, `pf_norm.c`,
`pf_table.c`, `pf_lb.c`, … ported from OpenBSD 8.0 sources) driven by the
real OpenBSD `pfctl` over the `/dev/pf` ioctl ABI. (A second module for the
`pfq` qdisc arrives with M5.)

**Status:** the filter core, stateful filtering, tables, anchors, NAT
(`nat-to`/`rdr-to`/`binat-to`), forced routing (`route-to`/`dup-to`), `divert`
and `pflog` are **verified in a real libvirt lab** (14/14 gates green).  The
remaining features (queue attachment/pacing, af-to) are still pending — honest per-feature status:
[`docs/en/feature-matrix.md`](docs/en/feature-matrix.md).
Demo (asciinema): [`docs/en/demo.md`](docs/en/demo.md).
Design: [`docs/superpowers/specs/2026-11-11-pf-linux-port-design.md`](docs/superpowers/specs/2026-11-11-pf-linux-port-design.md)
· plan: [`docs/superpowers/plans/2026-11-11-pf-linux-port.md`](docs/superpowers/plans/2026-11-11-pf-linux-port.md).

## Milestones

| Milestone | Scope | Status (2026-11-11) |
|---|---|---|
| M0 | repo, scripts, kernel build, VM boots the custom kernel | done |
| M1 | compat layer, `pf.ko` loads, `/dev/pf`, `pfctl -f` / `-s info` | done |
| M2 | filter core: rules/states/tables/anchors, VM gates 01–04 | **done** — gates 01/02/03/04 green |
| M3 | NAT (`nat-to`/`rdr-to`/`binat-to`), scrub | **done** — gates 05/06/07/09 (incl. 2 MB integrity through a redirect, min-ttl/max-mss measured) |
| M4 | route-to/dup-to/reply-to, divert e2e, pflog | **done** — divert (11), pflog (12), route-to/dup-to (08), pools (10), reply-to (13) verified |
| M5 | queues: HFSC/FQ-CoDel back ends + queue attachment | **partial** — both back ends now compile and link into `pf.ko` (suite still 14/14); priq remains attached, no bandwidth enforced yet |
| M6 | docs complete, patchset tidy, full suite green, tagged release | **done** — docs EN+PT-BR, 11/11 suite green, tagged (v0.5.0) |

## What you get (planned)

| Area | Feature |
|---|---|
| Filtering | rules, anchors, quick, `match`/`block`(drop/return/return-rst)/`pass`, `keep state`, syncookies |
| State/Tables | stateful firewall engine, `pfr` tables (radix + hash, `pfctl -t`) |
| NAT | `nat-to` (port pools, sticky-port, static-port), `rdr-to`, `binat-to` |
| Scrub | fragment reassembly, overlap drop, `min-ttl`/`max-mss`/`df`/`no-df`, `set reassemble` |
| QoS (ALTQ successor) | `queue`/`queue on` with OpenBSD 8.0 `ifq` schedulers: **default (priq), HFSC, FQ-CoDel**, attached via the `pfq` qdisc; `pfctl -s queue -v` stats |
| Divert | `divert-to` / `divert` tag + `pdivert` relay over an in-module `PF_DIVERT` socket family (verified: packet to userspace and back) |
| Route | `route-to`/`dup-to`/`reply-to` + pools (`round-robin`, `source-hash`) verified (`fail-to` no longer exists in OpenBSD 8.0) |
| Logging | `pflogd` via `/dev/pflog` (DLT_PFLOG pcap, OpenBSD record layout) — verified |
| Control | `/dev/pf` (0600), OpenBSD 8.0 ioctl ABI, Linux build of `pfctl` (single binary) |

Not in scope: `pfsync`/CARP failover, IPsec/pfkey, `ftp-proxy`, per-netns PF
instances, classic-ALTQ-only schedulers (cbq/wfq/red — removed upstream before
OpenBSD 8.0).

## Quick start

Full recipe: [`docs/en/install.md`](docs/en/install.md). Short version
(commands are read from the scripts; a fresh-clone end-to-end run is still
pending):

```sh
./scripts/fetch-sources.sh          # OpenBSD 8.0 sources (C3SL mirror, sha256-pinned)
./scripts/vendorize.sh              # stage the pristine files we port
./scripts/build-kernel.sh           # kernel 7.3.4: bzImage + modules_prepare (export patch: none needed)
./scripts/build-module.sh           # -> module/pf.ko
make -C tools                       # -> tools/out/pfctl (static), ABI harness
sudo insmod module/pf.ko            # /dev/pf (0600) + netfilter hooks
sudo pfctl -f /etc/pf.conf && sudo pfctl -e
```

Read [`docs/en/pf-guide.md`](docs/en/pf-guide.md) **before** enabling PF on a
path you need: only the M2 filter core is gated today; NAT/scrub/queues/
divert/pflog are not usable yet ([`docs/en/feature-matrix.md`](docs/en/feature-matrix.md)).

VM validation lab (Alpine on libvirt): [`docs/en/vm-lab.md`](docs/en/vm-lab.md).
Documentação em português: [`README.pt-BR.md`](README.pt-BR.md) e
[`docs/pt-BR/`](docs/pt-BR/README.md).

## Layout

- `module/` — kernel sources (ported PF + compat layer; the `pfq` qdisc lands with M5)
- `tools/` — pfctl (static Linux build), pdivert relay source, ABI harness (`pflogd` not ported yet)
- `patches/kernel/` — Linux core patch set (currently empty; evidence in `p1-needs.txt`, see `docs/en/patches.md`)
- `scripts/` — fetch/build/patch/VM automation
- `tests/` — VM suite (`tests/vm/NN-*.sh`, scapy helpers)
- `docs/en/` + `docs/pt-BR/` — full bilingual documentation
- `docs/superpowers/` — design spec + implementation plan

## License

Ported OpenBSD code retains its original [ISC license and copyright notices](LICENSE).
New Linux-side code: ISC.
