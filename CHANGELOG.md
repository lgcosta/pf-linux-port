# Changelog

## Unreleased

### Lab hygiene fixes (found while reproducing the issues)

- The gate helpers pinned pf0's management address to `192.168.122.8`. That
  address is a DHCP lease on the **shared** libvirt `default` network, so it
  moves whenever other VMs (e.g. the NewFirewall appliance) come and go — the
  gates then failed with "No route to host" for a reason that had nothing to
  do with PF. `tests/vm/lib.sh` and `scripts/vm-run-tests.sh` now resolve the
  address from the hypervisor (`virsh domifaddr`).
- The guest hops used `StrictHostKeyChecking=accept-new`, which *fails* when
  the host key changed — and the guests are recreated by `vm-create.sh`, so
  their keys change on every lab rebuild. The guest hops now use
  `StrictHostKeyChecking=no` with a throwaway known-hosts file, like
  `scripts/lab-lib.sh` already did for its own hops.

### Fixes for the NewFirewall integration issues

**Issue #2 — dynamic address pools (`nat-to (iface)`) follow the interface.**
Two defects, both reproduced in the lab and now fixed:

- The table behind a dynamic pool was rebuilt when an interface was
  attached or a group changed, but **not** when an address was added or
  removed — Linux exports no IP-address notifier to modules
  (`inetaddr_chain` is private to the network core, `NETDEV_CHANGEADDR`
  covers MAC changes only). A ruleset applied before the WAN address
  existed therefore kept an **empty** pool (no translation at all) and one
  applied before a DHCP change kept the **old** address, until the next
  ruleset reload — the boot race reported from the appliance. The compat
  layer now polls a cheap address fingerprint once a second and re-publishes
  the affected pools (`pfk_if_addr_poll_work()` → `pfi_kif_addr_refresh()`).
- The emulated **`egress`** group resolved to *nothing*, so `nat-to (egress)`
  never translated at all. It now resolves to the default-route interface —
  what OpenBSD's group holds.
- Gate `17-dynaddr.sh` proves it: no translation while the interface has no
  address, translation **without a ruleset reload** within a second of the
  address landing, and `nat-to (egress)` resolving to the default-route
  address.

**Issue #1 — the state-match lifetime assumption.** `pf_lock`/`pf_state_lock`
are OpenBSD's *own* locks (upstream defines `PF_LOCK()` as
`rw_enter_write(&pf_lock)`), and upstream's `pf_test()` never takes the
global `NET_LOCK` either — the comment in `pf.c` claiming otherwise is
inherited, stale upstream text. The rule, anchor and ruleset collected by
the state-match path are protected by the **refcount**: the state reference
(`pf_state_ref()`) keeps `states_cur` on the rule and anchor, and
`pf_rm_rule()` refuses to free a rule while `states_cur > 0`. The stale
comment now states this, and `caveats.md` §3 spells out what is covered and
what the narrow residual is (the path where no state was created).

### Benchmark vs nftables / iptables (and the module fixes it forced)

`bench/` adds a reproducible comparison of the port against **nftables**,
**iptables-nft** and **iptables-legacy** on one isolated bridge
(`bench-kedge -> bench-fw -> bench-srv`), with the firewall booting the
project's 7.3.4 kernel rebuilt with `NF_TABLES`, `NFT_*` and
`IP_NF_IPTABLES_LEGACY` so all four backends run on the same kernel and VM.

- `bench/lab/provision.sh` builds the lab; `bench/run.sh` + `bench/lab/bench-on-deb.sh`
  run the sweep (stateless, stateful, NAT, rule scale, reload under traffic,
  unbounded ceiling); `bench/tools/report.py` and `bench/tools/make_pdf.py`
  render the Markdown report, the static site and the A4 PDF from the *same*
  CSV.
- Report: `docs/pt-BR/benchmark.md` (with the EN executive summary),
  `docs/en/benchmark.md`, `bench/report/site/index.html`,
  `bench/report/benchmark.pdf`, raw samples in `bench/report/data/`.

Two port defects surfaced while building it and are fixed here:

- **`insmod` deadlocked on a kernel where the default route needs the FIB.**
  `pfk_egress_refresh()` did a route lookup from the netdevice notifier —
  and `register_netdevice_notifier()` calls its notifiers *under*
  `rtnl_lock()`, which the lookup takes again (self-deadlock; the guest
  hung on `insmod`). The egress interface is now resolved from the address
  poll (workqueue, no rtnl held, only the interface *index* is kept).
- `ip_rt_get_default_dev()` returned a `net_device *` that outlived its
  `dst_entry`; the helper now returns the interface index and releases the
  route before the caller uses it.
- **A loaded `pf.ko` silently disabled netfilter.** With PF turned off
  (`pfctl -d`) the hook still stole every packet onto the workqueue, so it
  never reached the later hooks: an `iptables`/`nftables` `FORWARD -j DROP`
  on the same host stopped matching the moment the module was loaded (the
  traffic flowed, the rule's counters stayed at zero). `pfk_enqueue_verdict()`
  now returns `NF_ACCEPT` unless `pf_status.running`, i.e. `pfctl -d` really
  means "hands off" and the native stack keeps working. This is what the
  functional matrix caught: it now shows `pass` for PF, nftables,
  iptables-nft and iptables-legacy on IPv4 filtering, stateful filtering and
  SNAT, with PF and nftables alone on native address sets.

Known measurement limit, documented in the report: PF-on-Linux is not
measurable under unbounded load — its workqueue backlog exhausts the guest
above roughly 1 Gbit/s (see caveats §13) — so the ceiling scenario runs on
the native backends only.

### Benchmark: NAT, rule scale and reload under traffic

The remaining sweep cells now have data (2 repetitions, reversed order per
rep): NAT at the 400 Mbit/s offered rate for all five backends, 200-rule
scale with the accept rule at the start/middle/end, and rule reload under a
TCP stream for the four native backends. PF's reload and stateful cells still
stall the guest (the ordered workqueue's backlog): both are recorded in
`bench/report/data/meta.json` with the reproduction command rather than
published as results.

Harness defects fixed on the way: the built-in policies were left at DROP by
a previous cell and silently poisoned the next one; iptables stateful cells
needed conntrack loaded; a blocked ping used to be published as 0.0 ms of
latency; the reload loop ran from the harness (two SSH round-trips per load,
~300 ms) and now runs inside the firewall (~1-9 ms per load); this iperf3
build has no `lost_percent`, so reload reports TCP retransmits (0 in every
cell). The rule-scale cells use one 100 Mbit/s stream, so their 0.100 Gbit/s
is exactly the offered load.

### Debian 13 implementation manual

`docs/en/debian13.md` + `docs/pt-BR/debian13.md` — the complete from-zero
path on Debian 13 (trixie): host preparation, kernel 7.3.4, `pf.ko`, the
userland, the libvirt lab, the gate suite and the stress test. Every command
was executed on the lab's Debian 13.7 host; the toolchain output, the
kernel.org checksum and the measured numbers are the real ones.

- `scripts/debian13/prepare-host.sh` — idempotent host preparation
  (packages + toolchain report). Verified on trixie: gcc 14.2.0, make 4.4.1,
  bison 3.8.2, flex 2.6.4, libvirt 11.3.0, QEMU 10.0.13.
- **Two build-script bugs fixed** (they broke the plain
  `./scripts/build-kernel.sh` invocation the manual documents):
  `scripts/kernel-config.sh` resolved its default kernel tree to
  `../../linux_kernel/linux-7.3.4` (one level too many — it only worked when
  `KERNELDIR` was exported), and `scripts/build-kernel.sh` did not pass the
  tree it resolved on to `kernel-config.sh`, so the two could configure
  different trees. Both now agree on `<repo>/../linux-7.3.4`.
- **gcc note**: Debian 13 ships gcc 14 and has no `gcc-15` in trixie. The
  7.3.4 kernel and `pf.ko` build with gcc 14 — no patch needed.

### Stress test (`tests/vm/16-stress.sh` + `scripts/stress/run-stress.sh`)

The project had no performance/load gate; the stress is a new gate that
asserts *invariants under load* and reports throughput informationally, per
the design decision that functional gates are the bar:

| Phase | Assertion |
|---|---|
| baseline / overhead | throughput with the module out of the path vs. with PF enabled (information + a configurable overhead limit) |
| state churn | states appear under 300 concurrent connections and drain back after `pfctl -F s` (no state-table leak) |
| rule churn | 200 reloads of a 200-rule ruleset, timed inside the VM, zero failures, stable rule count |
| table churn | 2000 `pfr` entries added and flushed back to 0 |
| integrity | 2 MB through `nat-to` under a concurrent stream — sha256 must match |
| health | no `BUG/WARNING/Oops`, PF counters moving, the firewall still routing |

Report and raw log land in `out/stress/<timestamp>/report.md`; the guest's
serial console is captured during the run so a panic is diagnosable.

Measured on the lab (full profile): 400 Mbit/s sustained with PF enabled at
0% overhead against the no-module baseline, 300/300 concurrent flows tracked
as states and the table drained back to 0, 200 reloads of a 202-rule ruleset
with zero failures (11 ms average, 20 ms worst), 2000 `pfr` entries flushed
back to 0, 2 MB through NAT byte-identical under load, and no
`BUG/WARNING/Oops` in `dmesg`.

### Capacity finding: the workqueue backlog OOMs the guest above ~1 Gbit/s

Measured while building the stress test, and now documented in
`docs/*/caveats.md` §13: PF steals every skb onto one ordered workqueue, so
the backlog grows with the offered rate. On the 1 GB lab guest, 400 Mbit/s
through a stateful ruleset is stable and ~1 Gbit/s **panics the kernel**
("System is deadlocked on memory"). The stress test therefore runs its
traffic phases at a bounded rate; the functional gates are unaffected.

- **M5 (first step)**: OpenBSD's `hfsc.c` and `fq_codel.c` now compile and link
  into `pf.ko` (157 scheduler symbols reachable from `pf_ioctl`).  Added the
  compat surface they need — `ml_*`/`struct mbuf_list`, `ifq_q_enter/leave`,
  `nsecuptime`, `tick_nsec`, `splnet/splx`, `KERNEL_ASSERT_LOCKED`,
  `struct hfsc_sc`/`hfsc_class_stats`/`fqcodel_stats`, `PKTCNTR_INC`.  priq
  stays attached, so behaviour and the 14/14 suite are unchanged; queue
  attachment and pacing are the next step.

## v0.6.0 — 2026-11-11

Adds scrub, route pools and `reply-to` verification.  Suite: 14 gates, all green.

| Capability | Gate | Proof |
|---|---|---|
| `reply-to` (asymmetric reply path) | `13-replyto` | a flow's reply is forced onto the gateway's link and never uses the client's link (measured on the firewall's own interfaces) |

| Capability | Gate | Proof |
|---|---|---|
| `scrub` normalization | `09-scrub` | `min-ttl 100` raises the TTL (egress shows 99 after the forward hop decrement); `max-mss 500` clamps a SYN's MSS option — measured on the firewall's own egress |
| `route-to` pools | `10-pools` | with two gateways on two links, `round-robin` uses both, `source-hash` pins every flow from one source to a single link |

Also: `docs/{en,pt-BR}/patches.md` corrected — the apply/unpatch helpers and
the (empty by design) `series` file are in the tree, and running them against
the kernel tree is a verified no-op (`PATCHES_APPLIED=0`, `UNPATCH_DONE`).

## v0.5.0 — 2026-11-11

Adds `binat-to`, `route-to`/`dup-to` and the checksum correctness work.
Suite: 11 gates, all green in the libvirt lab.

### Verified in the lab (new since v0.4.0)

| Capability | Gate | What it proves |
|---|---|---|
| `binat-to` (1:1 bidirectional NAT) | `07-binat` | pfctl's expansion into `nat-to … static-port` + `rdr-to` both work: the peer sees the mapped address, the original never leaks, the reply is translated back |
| `route-to` (forced nexthop) | `08-routeto` | the packet leaves through the gateway's link instead of the normal egress (proved on the firewall's own interfaces) |
| `dup-to` (packet duplication) | `08-routeto` | the original path keeps working and a copy is emitted to the gateway |
| bulk data through translated flows | `06-rdr` | 2 MB pulled through a redirect, byte-identical, firewall still responsive afterwards |

### Fixed

- **Neighbour resolution for forced routes**: Linux only ARPs the gateway when
  the route declares one, so `route-to` ARPed the *final* destination on the
  gateway's link. The port now sets `rt_gw4`/`rt_gw_family` on the looked-up
  route (the same pattern OVS uses).
- **`route-to` looked the route up with the packet's source**, which is not
  local for transit traffic → `ENETUNREACH` → silent drop. Destination-only
  lookup now.
- **`if_output_tso` reference imbalance**: the stack's output path consumed the
  hook queue's skb reference, so the work queue's cleanup double-freed the skb
  and corrupted the slab (observed as the firewall dying mid-test).
- **GSO/TSO checksums**: materializing a transport checksum in software for a
  translated skb covers only the first segment; every later segment is then
  wrong, bulk transfers stall and the firewall's own `sshd` starves. The port
  now recomputes in software only for non-GSO skbs and hands `CHECKSUM_PARTIAL`
  back to the NIC otherwise (regression guarded by gate `06-rdr`).
- Gate `08` counts only echo *requests* (a reply also contains the client
  address) and tolerates a not-yet-created capture file.

### Notes

- The lab's host bridge drops *forwarded* frames that cross two libvirt
  networks with a foreign source address, so `route-to`/`dup-to` are proved on
  the firewall's own interfaces rather than on `gw2`.

## v0.4.0 — 2026-11-11

Lab-verified milestone release.  The full gate suite (9 gates) runs against
Alpine VMs under libvirt on the kernel built from source.

### Verified in the lab (each has a gate in `tests/vm/`)

| Capability | Gate | What it proves |
|---|---|---|
| Engine loads, `/dev/pf`, real OpenBSD `pfctl` over the ioctl ABI | `01` | `insmod` + `pfctl -s info` + transit ping + state created |
| `block` / `return` / `return-rst` | `02` | ICMP dropped, TCP refused |
| `pfr` tables (inline `{ }`, `-T add/del/show`, `-T` block) | `03` | table load, block by table, delete, unblock |
| Anchors and evaluation order | `04` | anchor child passes the request before the parent `block`, state created by the anchor rule, request dropped after the anchor is flushed |
| `nat-to` (source NAT) | `05` | tcpdump proof of the rewritten source, reply de-NATed, state shows both tuples |
| `rdr-to` (port redirect) | `06` | client talks to the firewall, server answers, **2 MB pulled through the redirect byte-identical**, firewall still responsive afterwards |
| `divert-to` + `AF_DIVERT` + `pdivert` | `11` | drop with no listener (BSD `divs_noport` parity), packet delivered to userspace, rewritten and reinjected |
| `pflog` + `pflogd` | `12` | logged packets read from `/dev/pflog` and written as a `DLT_PFLOG` pcap containing the real bytes |
| Soak / unload discipline | `14` | traffic + states with no kernel warnings, clean `rmmod`, reload, no slab growth |

### Added

- `module/pfsrc/pfk_pflog.c` — `/dev/pflog` chardev backend (OpenBSD
  `pflog_hdr` layout, 64-byte records, bounded queue, `poll`, stats ioctl).
- `tools/pflogd` — reads `/dev/pflog`, writes `DLT_PFLOG` pcap (or a readable
  one-line-per-record summary).
- `tools/pdivert` — divert(4) relay with payload mutation + ICMP checksum fix.
- `module/pfsrc/divert_sock.c` — `AF_DIVERT` socket family registered at
  runtime (`PF_DIVERT = 32`, `IPPROTO_DIVERT = 254`), blocking `recvmsg`,
  `poll`, BSD-compatible reinjection.
- `scripts/demo-session.sh` + `scripts/demo-record.py` + `make demo` —
  asciinema recording of the live lab (`out/demo-pf-linux.cast`).
- `docs/{en,pt-BR}/`: README, install, build, guide, vm-lab, feature-matrix,
  patches, caveats, demo.  `docs/porting-notes.md` records every deviation.
- `patches/kernel/` + `scripts/apply-patches.sh` / `scripts/unpatch.sh` —
  kernel-core patch mechanism, empty series by design (verified: the module
  needs no core patch).

### Fixed (all found by the VM gates, root causes documented)

- `skb->mark` bookkeeping used multi-bit values, so the "in pass done" mark
  also satisfied the "out pass done" test: transit traffic never reached the
  **out** ruleset (NAT and egress rules silently ignored).
- The out ruleset was never evaluated for forwarded packets: added the
  `NF_INET_FORWARD` hook (OpenBSD evaluates it in `ip_forward()`).
- `refcnt_rele()` had inverted polarity → live state keys freed → `Oops` in
  the state RBT and `pfctl -s states` hangs.
- `radix` `SALEN()` read the `sa_family` byte as a length (Linux sockaddrs
  have no `sa_len`) → table entries dropped as duplicates.
- BSD/Linux `LIST_HEAD` macro arity clash in `compat/sys/queue.h`.
- Reverse NAT: a packet whose destination pf rewrote was delivered to the
  firewall's own stack instead of being re-routed.
- Checksums across translation: IP header checksum recomputed (OpenBSD's
  `ip_output` does this, Linux does not); transport checksum recomputed in
  software for plain skbs and handed back to the NIC (`CHECKSUM_PARTIAL`) for
  GSO/TSO — the earlier software-only fix corrupted every segment after the
  first and stalled bulk transfers.
- `divert`: NULL dereference on an empty listener list (wedged the VM),
  port byte order mismatch between `bind` and the queue, L3-only skbs injected
  into an Ethernet device RX path, re-divert loop (solved with a skip mark).
- `IOC_SIZEBITS` = 14 truncated the rule ioctl struct → `EINVAL` on load.
- State iteration started at id 1 (kifs never attached), `stoeplitz_init()`
  never called, purge timer re-arming itself at `rmmod`, `rtnl`×`NET` lock
  order inversion in the interface notifier.

### Known gaps

`binat-to`, `route-to`/`dup-to`/`fail-to`, `af-to`, scrub end-to-end
(normalization code is ported and compiles; the lab gate is not written),
queues/ALTQ (`hfsc.c`/`fq_codel.c` staged, `pfq` qdisc not implemented),
`pfsync`/CARP (out of scope).  See `docs/en/feature-matrix.md`.

## v0.3.0 — 2026-11-11

M2 data path: netisr-style workqueue, per-direction marks, state/table/anchor
gates passing; divert design settled (in-module socket family, no core patch).

## v0.2.0 — 2026-11-11

M1: engine compiles into `pf.ko` (pf.c, pf_ioctl.c with the full ioctl ABI,
pf_norm, pf_if, pf_lb, pf_table, pf_ruleset, pf_osfp, pf_syncookies, radix,
md5/sha, ifq), `/dev/pf`, Linux-built OpenBSD `pfctl` talking to it.

## v0.1.0 — 2026-11-11

M0: repo, bilingual scaffold, kernel 7.3.4 build, libvirt Alpine lab with
custom-kernel VMs (pf0/cli/srv/gw2), build/deploy/test scripts.
