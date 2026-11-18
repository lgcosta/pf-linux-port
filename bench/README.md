# Benchmark: PF-on-Linux vs nftables / iptables

Reproducible comparison of the PF-on-Linux port against the native Linux
backends — **nftables**, **iptables-nft** and **iptables-legacy** — plus a
no-filter baseline, on the same traffic path and the same kernel.

Reports generated from the same data:

| Format | Path |
|---|---|
| Markdown (pt-BR + EN summary) | [`docs/pt-BR/benchmark.md`](../docs/pt-BR/benchmark.md) |
| PDF (A4) | [`report/benchmark.pdf`](report/benchmark.pdf) |
| Static site | [`report/site/index.html`](report/site/index.html) |
| Raw samples | [`report/data/results.csv`](report/data/results.csv) |
| Medians / spread | [`report/data/summary.json`](report/data/summary.json) |

## Topology

Three VMs on an isolated bridge (`bench-net`, `forward=none`):

```
  bench-kedge            bench-fw                     bench-srv
  10.0.0.2   ─────────►  10.0.0.1 | 10.0.1.1  ──────►  10.0.1.2
                          (ip_forward=1)
  management: 192.168.122.201        192.168.122.203        192.168.122.202
```

`bench-fw` boots the project's self-built **7.3.4** kernel (`out/linux-7.3.4-bzImage`)
rebuilt with `NF_TABLES`, `NFT_*` and `IP_NF_IPTABLES_LEGACY` enabled, so the PF
module and the three native backends all run on that one kernel and VM.

## Run

```sh
./bench/lab/provision.sh       # create the bridge, the 3 VMs, install the backends
./bench/run.sh                 # sweep (BENCH_REPS=5 by default), writes data/run-<ts>/
python3 bench/tools/report.py  # regenerate Markdown + site from that CSV
python3 bench/tools/make_pdf.py
```

Useful knobs: `BENCH_REPS`, `BENCH_SECS`, `BENCH_RATE` (offered TCP rate per
stream), `BENCH_SCENARIOS`, `BENCH_BACKENDS`, `BENCH_SCALE_RULES`,
`BENCH_RELOADS`.

## Scenarios

| Scenario | What it does |
|---|---|
| `stateless` | simple accept rules; TCP throughput, UDP pps/loss, latency |
| `stateful` | `keep state` / `ct state`; throughput, connections/s, latency |
| `nat` | source NAT out the egress interface; throughput, latency |
| `scale` | N rules with the matching rule at the start / middle / end of the ruleset |
| `reload` | apply time and packet loss while a TCP stream crosses the firewall |
| `ceiling` | unbounded offered load, native backends only (PF is not measurable there) |

## Notes on the environment

- The **host** (libvirt hypervisor) is Debian 13; the **guests** are Alpine,
  the project lab's base image. The kernel under test is always the
  project's 7.3.4.
- Traffic generator and firewall share the hypervisor's CPU, so absolute
  throughput is bounded by the host, not by a physical link.
- PF-on-Linux cannot be measured under unbounded load: its data path runs
  every packet through a single ordered workqueue, and above roughly
  1 Gbit/s the backlog exhausts the guest (see
  [`docs/pt-BR/caveats.md`](../docs/pt-BR/caveats.md) §13). The sweep gives
  each traffic command a hard timeout, so a wedged PF cell is recorded as
  "no data" (an empty CSV value, shown as `—` in the reports) instead of
  stalling the run.
