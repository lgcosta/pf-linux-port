# Benchmark: PF-on-Linux vs nftables / iptables

Commit `9db0662` (local changes present). The full report (pt-BR) lives in [`docs/pt-BR/benchmark.md`](../pt-BR/benchmark.md); the site is [`bench/report/site/index.html`](../../bench/report/site/index.html) and the PDF [`bench/report/benchmark.pdf`](../../bench/report/benchmark.pdf).

## Executive summary

- Offered load 400 Mbit/s (4 x 100 Mbit/s TCP). nftables 0.40 Gbit/s, PF-on-Linux 0.40 Gbit/s; the unbounded no-filter baseline reaches 18.48 Gbit/s.
- PF-on-Linux passes every functional row the runner produced (IPv4 filtering, stateful filtering, SNAT, native address set) - the same observable result as nftables, and it is the only iptables-class backend with a native set (iptables needs ipset).

## Matriz funcional

Cada linha é um teste do `bench/lab/functest-on-deb.sh` sobre o mesmo caminho de trânsito (`bench-kedge -> bench-fw -> bench-srv`); a evidência de cada célula está no próprio runner e em `bench/report/data/functional.csv`.

| recurso | PF-on-Linux | nftables | iptables-nft | iptables-legacy |
|---|---|---|---|---|
| `ipv4_filter` | ✅ pass | ✅ pass | ✅ pass | ✅ pass |
| `ipv6_filter` | — n/a | — n/a | — n/a | ✅ pass |
| `stateful` | ✅ pass | ✅ pass | ✅ pass | ✅ pass |
| `snat` | ✅ pass | ✅ pass | ✅ pass | ✅ pass |
| `addr_set` | ✅ pass | ✅ pass | — n/a | — n/a |


- Its workqueue data path cannot reach line rate, and above ~1 Gbit/s the backlog exhausts the guest (kernel panic).
