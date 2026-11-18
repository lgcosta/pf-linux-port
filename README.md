# PF on Linux

Port of the OpenBSD **PF** (packet filter) to **Linux 7.3.4** as an
out-of-tree kernel module — the real PF engine (`pf.c`, `pf_norm.c`,
`pf_table.c`, `pf_lb.c`, `hfsc.c`, … ported from OpenBSD 8.0 sources) driven by
the real OpenBSD `pfctl` over the `/dev/pf` ioctl ABI.

**Status:** the filter core, stateful filtering, tables, anchors, NAT
(`nat-to`/`rdr-to`/`binat-to`), forced routing (`route-to`/`dup-to`/`reply-to`),
pools, `divert`, `pflog` and the OpenBSD 8.0 **queues (HFSC)** are **verified
in a real libvirt lab** — 17/17 gates green. What is still pending is stated
per feature in [`docs/en/feature-matrix.md`](docs/en/feature-matrix.md).

Screencasts (EN + PT, narrated): [`docs/en/screencast.md`](docs/en/screencast.md).
Design: [`docs/superpowers/specs/2026-11-11-pf-linux-port-design.md`](docs/superpowers/specs/2026-11-11-pf-linux-port-design.md)
· plan: [`docs/superpowers/plans/2026-11-11-pf-linux-port.md`](docs/superpowers/plans/2026-11-11-pf-linux-port.md).

## Milestones

| Milestone | Scope | Status |
|---|---|---|
| M0 | repo, scripts, kernel build, VM boots the custom kernel | done |
| M1 | compat layer, `pf.ko` loads, `/dev/pf`, `pfctl -f` / `-s info` | done |
| M2 | filter core: rules/states/tables/anchors, VM gates 01–04 | **done** — gates 01/02/03/04 green |
| M3 | NAT (`nat-to`/`rdr-to`/`binat-to`), scrub | **done** — gates 05/06/07/09 (incl. 2 MB integrity through a redirect, min-ttl/max-mss measured) |
| M4 | route-to/dup-to/reply-to, pools, divert e2e, pflog | **done** — divert (11), pflog (12), route-to/dup-to (08), pools (10), reply-to (13) verified |
| M5 | queues: HFSC back end in the data path | **done (mechanical)** — gate 15: queues load, hfsc attaches, `pfctl -s queue -v` reports class stats, a 2 MB queued flow is counted by the class. `bandwidth` = HFSC linkshare (guarantee, not cap; 8.0 has no upperlimit syntax) |
| M6 | docs complete, patchset tidy, full suite green, tagged release | **done** — docs EN+PT-BR, 17/17 suite green, tagged (`v1.0.0`) |

## What you get

| Area | Feature |
|---|---|
| Filtering | rules, anchors, quick, `match`/`block`(drop/return/return-rst)/`pass`, `keep state`, syncookies |
| State/Tables | stateful firewall engine, `pfr` tables (radix + hash, `pfctl -t`) |
| NAT | `nat-to` (port pools, sticky-port, static-port), `rdr-to`, `binat-to` |
| Scrub | fragment reassembly, overlap drop, `min-ttl`/`max-mss`/`df`/`no-df`, `set reassemble` |
| QoS (ALTQ successor) | `queue`/`queue on` with OpenBSD 8.0 `ifq` schedulers: **HFSC verified in the data path** (gate 15), priq default, FQ-CoDel compiled; `pfctl -s queue -v` works |
| Divert | `divert-to` / `divert` tag + `pdivert` relay over an in-module `PF_DIVERT` socket family (verified: packet to userspace and back) |
| Route | `route-to`/`dup-to`/`reply-to` + pools (`round-robin`, `source-hash`) verified (`fail-to` no longer exists in OpenBSD 8.0) |
| Logging | `pflogd` via `/dev/pflog` (DLT_PFLOG pcap, OpenBSD record layout) — verified |
| Control | `/dev/pf` (0600), OpenBSD 8.0 ioctl ABI, Linux build of `pfctl` (single binary) |

Not in scope: `pfsync`/CARP failover, IPsec/pfkey, `ftp-proxy`, per-netns PF
instances, classic-ALTQ-only schedulers (cbq/wfq/red — removed upstream before
OpenBSD 8.0).

## Screencasts

Seven narrated screencasts per language (English and Portuguese), 1080p30 with
burned-in subtitles, the lab's network diagram drawn over the terminal, and a
continuous film that crossfades the phases together. Every video is a **real
shell session** against the Alpine VMs — commands typed key by key, the kernel
answering live — with a MiniMax female voice narrating:

```sh
./scripts/screencast/build-all.sh       # -> out/screencast/{en,pt}/*.mp4
```

Details, per-phase breakdown and the YouTube description text:
[`docs/en/screencast.md`](docs/en/screencast.md) ·
[`docs/pt-BR/screencast.md`](docs/pt-BR/screencast.md)

## Quick start

Benchmark vs the native backends (nftables, iptables-nft, iptables-legacy):
[`docs/en/benchmark.md`](docs/en/benchmark.md) ·
[`bench/`](bench/) · report [`bench/report/site/index.html`](bench/report/site/index.html).

Full recipe: [`docs/en/install.md`](docs/en/install.md). **Debian 13 (trixie)
from zero** (host prep → kernel → module → tools → lab → stress):
[`docs/en/debian13.md`](docs/en/debian13.md). Short version
(commands are read from the scripts):

```sh
./scripts/fetch-sources.sh          # OpenBSD 8.0 sources (C3SL mirror, sha256-pinned)
./scripts/vendorize.sh              # stage the pristine files we port
./scripts/build-kernel.sh           # kernel 7.3.4: bzImage + modules_prepare (export patch: none needed)
./scripts/build-module.sh           # -> module/pf.ko
make -C tools                       # -> tools/out/{pfctl,pdivert,pflogd,abitest}
sudo insmod module/pf.ko            # /dev/pf (0600) + netfilter hooks
sudo pfctl -f /etc/pf.conf && sudo pfctl -e
```

Every feature listed above is exercised by the lab suite before you need it;
[`docs/en/feature-matrix.md`](docs/en/feature-matrix.md) says which gate proves
what, and [`docs/en/pf-guide.md`](docs/en/pf-guide.md) lists the deviations you
should know before putting PF on a path that matters.

VM validation lab (Alpine on libvirt): [`docs/en/vm-lab.md`](docs/en/vm-lab.md).
Documentação em português: [`README.pt-BR.md`](README.pt-BR.md) e
[`docs/pt-BR/`](docs/pt-BR/README.md).

## Layout

- `module/` — kernel sources: ported PF engine (`module/pfsrc`) + Linux compat layer (`module/compat`)
- `tools/` — pfctl (static Linux build), pdivert relay, pflogd, ABI harness (`make -C tools abi`)
- `patches/kernel/` — Linux core patch set (currently empty; evidence in `p1-needs.txt`, see `docs/en/patches.md`)
- `scripts/` — fetch/build/patch/VM automation, plus `scripts/screencast/` for the videos
- `scripts/debian13/` — Debian 13 host preparation (`prepare-host.sh`)
- `scripts/stress/` — stress runner (`run-stress.sh`) + report writer
- `tests/vm/` — VM suite: 17 gates (`tests/vm/NN-*.sh`) + `lib.sh`
- `docs/en/` + `docs/pt-BR/` — full bilingual documentation
- `docs/superpowers/` — design spec + implementation plan

## License

Ported OpenBSD code retains its original [ISC license and copyright notices](LICENSE).
New Linux-side code: ISC.
