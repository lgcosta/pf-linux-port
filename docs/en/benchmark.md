# Benchmark: PF-on-Linux vs nftables / iptables

Commit `16c7c64` (local changes present). The full report (pt-BR) lives in [`docs/pt-BR/benchmark.md`](../pt-BR/benchmark.md); the site is [`bench/report/site/index.html`](../../bench/report/site/index.html) and the PDF [`bench/report/benchmark.pdf`](../../bench/report/benchmark.pdf).

## Executive summary

- Offered load 400 Mbit/s (4 x 100 Mbit/s TCP). nftables 0.40 Gbit/s, PF-on-Linux 0.40 Gbit/s; the unbounded no-filter baseline reaches 0.40 Gbit/s.
- PF-on-Linux clears the functional matrix it was measured on (IPv4 filtering, stateful filtering, address sets) with the same observable result as the native backends.
- Its workqueue data path cannot reach line rate, and above ~1 Gbit/s the backlog exhausts the guest (kernel panic).
