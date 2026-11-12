# Feature matrix

Português: [docs/pt-BR/feature-matrix.md](../pt-BR/feature-matrix.md).

Status vocabulary (used identically in both languages):

- **VERIFIED-in-VM** — passed end-to-end in the libvirt lab (see [vm-lab.md](vm-lab.md)); the gate that proves it is named.
- **VERIFIED-offline** — proven by a fixture/ABI test, not by traffic (e.g. `make -C tools test`).
- **COMPILES-untested** — built into `pf.ko`/`pfctl`; no gate has exercised it.
- **NOT-ENABLED** — the code exists but the feature is off in this build (e.g. `af-to`, which needs the `INET6` paths).
- **OUT-OF-SCOPE** — non-goal by design (spec §3).

As of 2026-11-12; source of per-row evidence: `out/test-logs/`,
`docs/porting-notes.md`, milestone commits, and the screencasts
([screencast.md](screencast.md)) for the visible proof.

The suite is **15/15 green** (`./scripts/vm-run-tests.sh`).

## Control plane & tools

| Feature | Upstream syntax | Milestone | Status |
|---|---|---|---|
| Module load, `/dev/pf` (0600) | `insmod pf.ko` | M1 | VERIFIED-in-VM (gate `02-ativacao.sh`) |
| Enable/disable | `pfctl -e`, `pfctl -d` | M1 | VERIFIED-in-VM |
| Ruleset load | `pfctl -f file` | M2 | VERIFIED-in-VM (every gate loads a real ruleset) |
| Parse-only check | `pfctl -n -f file` | M1 | VERIFIED-offline (`make -C tools test`) |
| Status/counters | `pfctl -s info [-v]` | M1 | VERIFIED-in-VM |
| Rules listing | `pfctl -s rules [-v]`, `pfctl -sr` | M2 | VERIFIED-in-VM (gates 02–06) |
| States listing | `pfctl -s states [-v]`, `pfctl -ss` | M2 | VERIFIED-in-VM (gate `01-state.sh`) |
| Tables listing/ops | `pfctl -s tables`, `-t t -T show/add/del/kill` | M2 | VERIFIED-in-VM (gate `03-tables.sh`, incl. a live `-T delete` unblocking the flow) |
| Anchors | `pfctl -a anchor -f file`, `-s anchors` | M2 | VERIFIED-in-VM (gate `04-anchors.sh`) |
| Queue listing | `pfctl -s queue -v` | M5 | VERIFIED-in-VM (gate `15-queue.sh`: both classes with statistics) |
| ioctl ABI parity | `make -C tools abi` (abitest tables) | M1 | VERIFIED-offline (userland table); the kernel-side diff is an open integrator step (`tools/UPSTREAM.md`) |

## Filtering core

| Feature | Upstream syntax | Milestone | Status |
|---|---|---|---|
| `pass`/`match`, quick, on-interface/proto/addr/port | `pass in quick on wan proto tcp from 10/8 to port 80` | M2 | VERIFIED-in-VM (gate `01-state.sh`: transit ping through the firewall) |
| `block drop` | `block drop in quick …` | M2 | VERIFIED-in-VM (gate `02-block.sh`: ping dies, counters prove it) |
| `block return` / `return-rst` / `return-rst after quarter` | `block return-rst in quick proto tcp …` | M2 | VERIFIED-in-VM (gate `02-block.sh`: the client's `nc` gets an RST) |
| `keep state` (icmp/tcp/udp), state table, purger | `… keep state` | M2 | VERIFIED-in-VM (gate `01-state.sh`; state visible in `pfctl -ss`) |
| `set state-policy`, `allow-opts`, etc. | `set …` options | M2 | COMPILES-untested |
| Syncookies (state overflow path) | `set syncookies always` | M2 | COMPILES-untested |
| pfr tables (radix/hash) | `table <blk> persist { 1.2.3.4 }`, `-T add …` | M2 | VERIFIED-in-VM (gate `03-tables.sh`) |
| Anchors + include + quick precedence | `anchor "x"`, `load anchor …` | M2 | VERIFIED-in-VM (gate `04-anchors.sh`) |
| IPv6 for all of the above | same syntax, v6 | M2+ | COMPILES-untested — the engine is v6-complete by construction, but the v6 path is not exercised by a gate |
| Non-IPv4/IPv6 traffic (ARP…) | — | M2 | PFLINUX by construction: netfilter IPv4/IPv6 hooks only — ARP never enters the PF path (no gate) |

## NAT / normalization (M3)

| Feature | Upstream syntax | Milestone | Status |
|---|---|---|---|
| `nat-to` (port pools, sticky-port, static-port) | `pass out … nat-to (eth1:0)` | M3 | **VERIFIED-in-VM** — gate `05-nat.sh`: source NAT works end to end (tcpdump proof), the reply is de-NATed and re-routed to the client, and the state shows both tuples (`10.0.2.1:59335 (10.0.1.10:4380) -> 10.0.2.20:8`). |
| `rdr-to` | `pass in … to <fw> port 8080 rdr-to <srv> port 80 keep state` | M3 | **VERIFIED-in-VM** — gate `06-rdr.sh`: the client talks to the firewall's address, pf rewrites the destination, re-routes the packet, and the reply is un-redirected. The gate also pulls 2 MB through the redirect and checks byte-for-byte integrity (GSO/TSO regression guard). |
| `binat-to` | `pass quick proto icmp from 10.0.2.20 to 10.0.1.10 binat-to 10.0.2.50 keep state` | M3 | **VERIFIED-in-VM** — gate `07-binat.sh`. pfctl expands one `binat-to` rule into the documented pair (`pass out … nat-to <mapped> static-port` + `pass in … rdr-to <real>`), and both halves work: the peer sees the mapped address, the original never leaks, and the reply is translated back. |
| NAT pools (`round-robin`, `source-hash`) | `nat-to { 10.0.2.1, 10.0.3.1 } round-robin` | M4 | **VERIFIED-in-VM** — gate `10-pools.sh`, and visible in the screencast: round-robin alternates the source address between the members (3 packets each), source-hash keeps one origin on one member. |
| Scrub/normalize: reassembly, `fragment drop-overlap`, `min-ttl`, `max-mss`, `df`/`no-df`, `random-id`, `set reassemble yes` | `match in all scrub (min-ttl 100 max-mss 500)` | M3 | **VERIFIED-in-VM** — gate `09-scrub.sh`: `min-ttl` raises the TTL (egress shows 99 after the forward decrement) and `max-mss` clamps a SYN's MSS option, both measured on the firewall's own egress. Reassembly/overlap paths are compiled and reached but not gated. |
| OS fingerprinting in states | `pass … set os A` + `/etc/pf.os` | bonus | COMPILES-untested (`pf_osfp.c` compiled; no fingerprint packaging yet — a missing `/etc/pf.os` is tolerated) |

## Routing / divert / queues / logging

| Feature | Upstream syntax | Milestone | Status |
|---|---|---|---|
| `route-to` / `dup-to` / `reply-to` | `pass in … route-to { 10.0.3.2 } keep state`, `reply-to { gw }` | M4 | **VERIFIED-in-VM** — gates `08-routeto.sh` (route-to leaves via the gateway's link, dup-to copies while keeping the flow) and `13-replyto.sh` (the reply of a flow is forced onto the gateway link instead of the client's). Deviation: the forced nexthop is installed by setting `rt_gw4`/`rt_gw_family` on the looked-up route (Linux only ARPs the gateway when the route declares one). `fail-to` is **not a keyword in OpenBSD 8.0** (removed upstream), so it is out of scope. |
| `divert-to` / `divert` tag | `pass in … divert-to <host> port <n>` | M4 | **VERIFIED-in-VM** — gate `11-divert.sh`. The module registers an `AF_DIVERT` socket family (`PF_DIVERT=32`) at runtime — no kernel-tree patch. A `divert-to` rule hands the packet to that family; with no listener the packet is dropped (`divs_noport`, BSD parity); with `tools/pdivert` running the packet is delivered to userspace, rewritten and reinjected (verified with a payload mutation + checksum fix, ping 0% loss). |
| Queues (`queue`, `queue on`, `pfctl -s queue -v`) | `queue rootq on eth2 bandwidth 10Mbit` + `queue leafq on eth2 bandwidth 2Mbit parent rootq default` | M5 | **VERIFIED-in-VM** — gate `15-queue.sh`: the queue config loads, the hfsc discipline attaches to the interface, `pfctl -s queue -v` lists both classes with statistics, a 2 MB queued flow completes, and the class's packet counter advances (the scheduler is in the data path). Note `bandwidth` is HFSC *linkshare* — a guarantee under contention, not a cap: OpenBSD 8.0's pf.conf has no `minimum`/`maximum`, so a lone flow is not rate-limited, on Linux or on OpenBSD. |
| HFSC | `queue on wan { hfsc … }` | M5 | **VERIFIED-in-VM** — `pfsrc/hfsc.c` (real OpenBSD 8.0 code) is in the module object list and is the discipline the gate exercises. The class handles use the vendored `net/hfsc.h` (root class `0x10000`, default class `0x1000`) shared with `pfctl`, so the stats ABI matches on both sides. |
| FQ-CoDel | `queue on wan { fq_codel … }` | M5 | COMPILES-untested — `pfsrc/fq_codel.c` is built into `pf.ko` and the vendored `net/fq_codel.h` is shared with `pfctl`, but no gate drives it. |
| `pfq` interface | n/a (BSD `ifq`/`pfq` interface, not a Linux `tc` qdisc) | M5 | **VERIFIED-in-VM** — the port keeps the BSD shape: `compat/pfk_ifq.c` implements the `ifq_ops`/`pfq_ops` interfaces and the OpenBSD back ends plug into them (`pfq` is not a Linux `tc` qdisc and never was meant to be). priq is the default discipline. |
| pflog (`log`, `log all`, `block log`) + `pflogd` pcap (DLT_PFLOG) | `pass out quick log` + `pflogd /tmp/x.pcap` | M4 | **VERIFIED-in-VM** — gate `12-pflog.sh`: logged packets are queued by the engine, read through `/dev/pflog` and written as a pcap with linktype DLT_PFLOG (117); the dump contains the real packet bytes. Deviation: a chardev instead of an if_pflog/bpf interface (Linux has no bpf over such a virtual interface here); record layout is byte-compatible with OpenBSD `pflog(4)`. `if_pflog.c` stays unbuilt. |

## Non-goals (explicit)

| Item | Reason |
|---|---|
| `pfsync` / CARP failover | non-goal (spec §3) |
| IPsec / pfkey | non-goal |
| `ftp-proxy` and userland daemons | non-goal |
| Per-netns PF instances | faithful port = single global instance on `init_net` |
| Classic-ALTQ-only schedulers (cbq/wfq/red…) | removed upstream before OpenBSD 8.0 — cannot be ported from the base |
| Routing labels (`route-label`) | non-goal; ioctl layer rejects labeled rules |
| `af-to` (IPv4↔IPv6 translation) | the code sits under `#ifdef INET6` and this build has `INET6 0`; enabling it is a separate milestone, not a missing feature in the base |
