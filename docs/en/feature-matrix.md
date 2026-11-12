# Feature matrix

Português: [docs/pt-BR/feature-matrix.md](../pt-BR/feature-matrix.md).

Status vocabulary (used identically in both languages):

- **VERIFIED-in-VM** — passed end-to-end in the libvirt lab (see [vm-lab.md](vm-lab.md)).
- **M2-in-progress** — implemented; VM gates 01–04 exist but the suite is **not all green yet**; no VERIFIED claim until a clean full run.
- **COMPILES-untested** — built into `pf.ko`/`pfctl`; no gate has exercised it.
- **PENDING-Mx** — lands in milestone Mx; today's observable behavior is stated.
- **OUT-OF-SCOPE** — non-goal by design (spec §3).

As of 2026-11-11; source of per-row evidence: `out/test-logs/`,
`docs/porting-notes.md`, milestone commits.

## Control plane & tools

| Feature | Upstream syntax | Milestone | Status |
|---|---|---|---|
| Module load, `/dev/pf` (0600) | `insmod pf.ko` | M1 | VERIFIED-in-VM |
| Enable/disable | `pfctl -e`, `pfctl -d` | M1 | VERIFIED-in-VM |
| Ruleset load | `pfctl -f file` | M2 | VERIFIED-in-VM (loads succeeded in every captured gate run; run status itself is mixed — see [vm-lab.md](vm-lab.md) §6) |
| Parse-only check | `pfctl -n -f file` | M1 | VERIFIED (offline fixture suite, `make -C tools test`) |
| Status/counters | `pfctl -s info [-v]` | M1 | VERIFIED-in-VM |
| Rules listing | `pfctl -s rules [-v]` | M2 | COMPILES-untested |
| States listing | `pfctl -s states [-v]` | M2 | M2-in-progress — gate 01 shows a listing anomaly (transit ping OK, no icmp state displayed) while state machinery is being fixed |
| Tables listing/ops | `pfctl -s tables`, `-t t -T show/add/del/kill` | M2 | M2-in-progress (gate 03) |
| Anchors | `pfctl -a anchor -f file`, `-s Anchors` | M2 | M2-in-progress (gate 04) |
| Queue listing | `pfctl -s queue -v`, `-s ifq -v` | M5 | PENDING-M5 (parses fine; counters not wired to real back ends yet) |
| ioctl ABI parity | `make -C tools abi` (abitest tables) | M1 | VERIFIED build-side (userland table); kernel-side diff is an open integrator step (`tools/UPSTREAM.md`) |

## Filtering core

| Feature | Upstream syntax | Milestone | Status |
|---|---|---|---|
| `pass`/`match`, quick, on-interface/proto/addr/port | `pass in quick on wan proto tcp from 10/8 to port 80` | M2 | M2-in-progress (data path runs the real `pf_test`; latest partial run: transit ping OK, icmp block OK) |
| `block drop` | `block drop in quick …` | M2 | M2-in-progress (gate 02a) |
| `block return` / `return-rst` / `return-rst after quarter` | `block return-rst in quick proto tcp …` | M2 | M2-in-progress (gate 02b) |
| `keep state` (icmp/tcp/udp), state table, purger | `… keep state` | M2 | M2-in-progress |
| `set state-policy`, `allow-opts`, etc. | `set …` options | M2 | COMPILES-untested |
| Syncookies (state overflow path) | `set syncookies always` | M2 | COMPILES-untested |
| pfr tables (radix/hash) | `table <blk> persist { 1.2.3.4 }`, `-T add …` | M2 | M2-in-progress (gate 03) |
| Anchors + include + quick precedence | `anchor "x"`, `load anchor …` | M2 | M2-in-progress (gate 04) |
| IPv6 for all of the above | same syntax, v6 | M2+ | COMPILES-untested (engine is v6-complete by construction; v6 smoke gates planned) |
| Non-IPv4/IPv6 traffic (ARP…) | — | M2 | PFLINUX by construction: netfilter IPv4/IPv6 hooks only — ARP never enters PF path (no gate yet) |

## NAT / normalization (M3)

| Feature | Upstream syntax | Milestone | Status |
|---|---|---|---|
| `nat-to` (port pools, sticky-port, static-port) | `pass out … nat-to (eth1:0)` | M3 | **VERIFIED in lab** — source NAT works end to end: the packet leaves with the pool address (tcpdump proof), the reply is de-NATed and re-routed to the client, and the state shows both tuples (`10.0.2.1:59335 (10.0.1.10:4380) -> 10.0.2.20:8`). Gate: `tests/vm/05-nat.sh`. |
| `rdr-to` | `pass in … to <fw> port 8080 rdr-to <srv> port 80 keep state` | M3 | **VERIFIED in lab** — port redirect works end to end (gate `06-rdr.sh`): the client talks to the firewall's address, pf rewrites the destination, re-routes the packet, and the reply is un-redirected. The gate also pulls 2 MB through the redirect and checks byte-for-byte integrity (GSO/TSO regression guard). |
| `binat-to` | `pass quick proto icmp from 10.0.2.20 to 10.0.1.10 binat-to 10.0.2.50 keep state` | M3 | **VERIFIED in lab** — gate `07-binat.sh`. pfctl expands one `binat-to` rule into the documented pair (`pass out … nat-to <mapped> static-port` + `pass in … rdr-to <real>`), and both halves work: the peer sees the mapped address, the original never leaks, and the reply is translated back. |
| Scrub/normalize: reassembly, `fragment drop-overlap`, `min-ttl`, `max-mss`, `df`/`no-df`, `random-id`, `set reassemble yes` | `match in all scrub (min-ttl 100 max-mss 500)` | M3 | **VERIFIED in lab** — gate `09-scrub.sh`: `min-ttl` raises the TTL (egress shows 99 after the forward decrement) and `max-mss` clamps a SYN's MSS option, both measured on the firewall's own egress. Reassembly/overlap paths are compiled and reached but not yet gated. |
| OS fingerprinting in states | `pass … set os A` + `/etc/pf.os` | bonus | COMPILES-untested (`pf_osfp.c` compiled; no fingerprint packaging yet — missing `/etc/pf.os` is tolerated) |

## Routing / divert / queues / logging

| Feature | Upstream syntax | Milestone | Status |
|---|---|---|---|
| `route-to` / `dup-to` / `reply-to` | `pass in … route-to { 10.0.3.2 } keep state`, `reply-to { gw }` | M4 | **VERIFIED in lab** — gates `08-routeto.sh` (route-to leaves via the gateway's link, dup-to copies while keeping the flow) and `13-replyto.sh` (the reply of a flow is forced onto the gateway link instead of the client's). Deviation: the forced nexthop is installed by setting `rt_gw4`/`rt_gw_family` on the looked-up route (Linux only ARPs the gateway when the route declares one). `fail-to` is **not a keyword in OpenBSD 8.0** (removed upstream), so it is out of scope. |
| `divert-to` / `divert` tag | `pass in … divert-to <host> port <n>` | VERIFIED in lab | **Works end-to-end.** The module registers an `AF_DIVERT` socket family (`PF_DIVERT=32`) at runtime — no kernel-tree patch. A `divert-to` rule hands the packet to that family; with no listener the packet is dropped (`divs_noport`, BSD parity); with `tools/pdivert` running the packet is delivered to userspace, rewritten and reinjected (verified with a payload mutation + checksum fix, ping 0% loss). |
| Queues, default scheduler (priq) | `queue root on wan`, `pfctl -s queue -v` | M5 | COMPILES-untested — `compat/pfk_ifq.c` implements the ifq API with **priq fallback semantics**; `queue` rules parse and load, but packets are not actually shunted through a classful qdisc until M5 |
| HFSC | `queue on wan { hfsc … }` | M5 | COMPILES-untested — **priq fallback until M5**; `pfsrc/hfsc.c` staged (real OpenBSD 8.0 code) but **not in the module object list** |
| FQ-CoDel | `queue on wan { fq_codel … }` | M5 | COMPILES-untested — same as HFSC: staged, not built |
| `pfq` qdisc (`tc qdisc … root pfq`) | n/a (Linux glue) | M5 | PENDING-M5 (does not exist in the tree yet) |
| pflog (`log`, `log all`, `block log`) + `pflogd` pcap (DLT_PFLOG) | `pass out quick log` + `pflogd /tmp/x.pcap` | M4 | **VERIFIED in lab** — gate `12-pflog.sh`: logged packets are queued by the engine, read through `/dev/pflog` and written as a pcap with linktype DLT_PFLOG (117); the dump contains the real packet bytes. Deviation: a chardev instead of an if_pflog/bpf interface (Linux has no bpf over such a virtual interface here); record layout is byte-compatible with OpenBSD `pflog(4)`. `if_pflog.c` stays unbuilt. |

## Non-goals (explicit)

| Item | Reason |
|---|---|
| `pfsync` / CARP failover | non-goal (spec §3) |
| IPsec / pfkey | non-goal |
| `ftp-proxy` and userland daemons | non-goal |
| Per-netns PF instances | faithful port = single global instance on `init_net` |
| Classic-ALTQ-only schedulers (cbq/wfq/red…) | removed upstream before OpenBSD 8.0 — cannot be ported from the base |
| Routing labels (`route-label`) | non-goal; ioctl layer rejects labeled rules |
