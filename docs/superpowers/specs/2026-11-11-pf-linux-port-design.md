# PF on Linux — Port Design / Design do Port

Date/2026-11-11 · Status: awaiting approval/aguardando aprovação · Repo: https://git.labexposed.com/lgcosta/pf-linux-port (public)

## 1. Intent / Intenção

Run OpenBSD's PF packet filter inside Linux 7.3.4 — the real PF engine, driven by the
real `pfctl` speaking the real `/dev/pf` ioctl ABI — as an out-of-tree module with a
documented, minimal set of kernel core patches, validated end-to-end in a libvirt
Alpine VM. Docs bilingual (EN + pt-BR).

Rodar o PF do OpenBSD no Linux 7.3.4 — engine PF real, controlado pelo `pfctl` real
falando a ABI ioctl `/dev/pf` — como módulo out-of-tree com um conjunto mínimo e
documentado de patches no core, validado numa VM Alpine via libvirt.

## 2. Facts found / Fatos levantados

- OpenBSD **replaced classic ALTQ by ≥7.7** (verified: no `sys/net/altq` dir in
  7.7/7.8/8.0) — **but PF queueing lives on in 8.0**, rearchitected into the core
  `ifq` framework: `sys/net/ifq.c` (priq ops), `sys/net/hfsc.c`,
  `sys/net/fq_codel.c`, ioctls `DIOCADDQUEUE/DIOCGETQUEUES/DIOCGETQUEUE/
  DIOCGETQSTATS` (`pfvar.h:1707+`), `pf_rule.queue` glue all present; `pfctl`
  still parses/installs `queue on … hfsc|fq_codel|default`. Older ALTQ releases
  (≤7.6) are no longer mirrored anywhere (cdn/ftp/mirrors/GitHub-raw/Wayback all
  404), making a 7.x base infeasible/undesirable anyway.
  ⇒ **port base = OpenBSD 8.0** (exactly matches the user's provided userland
  tree, pfctl.c v1.406 2026-08-30; sources fetched from the user's C3SL mirror).
  The ALTQ requirement is met as **OpenBSD-8.0 queueing (priq + HFSC + fq_codel)**
  — the framework modern OpenBSD itself ships; classic cbq/wfq/red are
  non-goals *because they don't exist in the base*, honestly matrixed.
- Kernel side ≈ 24.6k LOC (`pf.c` 9.2k, `pf_ioctl.c` 4.6k, `pf_norm/if/lb/table/
  ruleset/osfp/syncookies`, `pfvar(_priv).h`) + queueing core (`ifq/hfsc/fq_codel`
  ≈ 2.4k LOC) + `ip_divert.c` reference (~300 LOC).
- Userland: single `pfctl` (~20k LOC incl. `parse.y`) — **libpfctl was folded back
  into pfctl** in this era (Makefile `LDADD+= -lm` only; no libpfctl in tree).
- Linux 7.3.4 tree clean; toolchain gcc 16/flex/bison/bc; libvirt 12 + KVM usable
  (existing `sms-vm` untouched); sudo works; Alpine cloud images available on dl-cdn.
- Module-visible APIs confirmed exported: `neigh_lookup`, `pskb_expand_head`,
  `skb_clone`, `register_qdisc`, `qdisc_tree_reduce_backlog`, `tcf_classify`,
  `__dev_queue_xmit`, `netif_rx`, `inet_addr_type`, `fib_table_lookup` (GPL).
- Confirmed **not exported** in 7.3.4: `neigh_output`, `dev_queue_xmit` (inline
  wrapper over `__dev_queue_xmit`), `ip_route_output_key`, `ip6_route_output`,
  `netlink_kernel_create` ⇒ core patch **P1 (mandatory): export symbols required
  by the PF module** (additive `EXPORT_SYMBOL_*`, zero behavior change) — allowed
  by user, fully documented in `patches/kernel/` + `docs/*/patches.md`.

## 3. Goals / Non-goals

**Goals (G)**
1. PF engine in-tree-equivalent quality, delivered as module `pf.ko` built from
   `pf_port/` sources: rules (anchors, quick, match/block/pass/drop, return variants,
   keep state), states + syncookies, tables (pfr: radix/hash, `pfctl -t`),
   NAT (nat-to, rdr-to incl. sticky-port/bitmap basics, binat-to, route-to),
   **scrub/normalize** (fragment reassembly, DF/TTL actions, min-ttl, max-mss),
   **PF queues/ALTQ semantics of modern OpenBSD 8.0** (`queue`, `queue on` with
   `default`(priq), `hfsc`, `fq_codel` schedulers from `sys/net/ifq.c`,
   `hfsc.c`, `fq_codel.c`; `pfctl -s queue -v` stats), **divert** (`divert-to`,
   `divert` tag; userspace channel modeled on `sys/netinet/ip_divert.c`),
   **route-to** (pools + rr/rr-source/rr-any, hash-source/state;
   `reply-to`, `dup-to`, `fail-to`).
2. Control plane = OpenBSD ioctl ABI on `/dev/pf`; Linux-built `pfctl` (8.0,
   single binary, no libpfctl) + `pflogd`-lite + `pdivert` tool.
3. Patchset + scripts in the repo; kernel core patches only when unavoidable, each
   documented (what/why/where/impact).
4. VM validation: kernel 7.3.4 built with patches, module + tools deployed into an
   Alpine VM booted with that kernel; a scripted suite exercises every goal-1 area
   with real traffic across a 4-VM libvirt lab (§7) and checks PASS/FAIL.
5. Full docs EN + pt-BR: apply patches, build, install, feature guide, VM validation
   how-to, feature matrix incl. exclusions.

**Non-goals (NG, explicit to user)**: pfsync/carp (no failover), full IPsec/pfkey,
classic-ALTQ-only schedulers (cbq/wfq/red — **do not exist in the OpenBSD 8.0 base**,
matrixed as "removed upstream"), queues on interfaces whose root qdisc is not ours,
per-netns PF instances (faithful port = single global instance like OpenBSD; netns
isolation documented as out of scope), OS-fingerprinting beyond `pfctl -o` (osfp is
bonus if cheap), `ftp-proxy` and other userland daemons.

**Anti-goals**: silently faking a feature (stub verdicts), nftables-as-substitute
claims, tests that only check exit codes without inspecting packets/state.

## 4. Approach (chosen) / Abordagem

**B′ — faithful port with a compat shim** (alternative C rejected: user wants PF in
kernel; A rejected: infeasible). Port the actual OpenBSD 8.0 `pf*.c` + `ifq/hfsc/
fq_codel` + `ip_divert`-referenced divert under a compatibility layer, hook into
Linux via netfilter, keep upstream ABI for userland.
(Rejected variant: rewrite of PF semantics on nf APIs = not a port.)

### 4.1 Compat layer (`module/compat/`)
- **Packet object**: `struct mbuf` shim over `sk_buff` (single-segment: linearize on
  entry to PF; `m_copym/m_adj/m_pull/m_copydata/m_cat/m_clump/m_free` → skb ops;
  `m_pkthdr` ↔ `skb->protocol/tc/index`, address family, ifindex via
  `skb->dev`). This mirrors how OpenBSD's own pf code treats mbufs and keeps diffs
  localized to packet I/O, not to matching logic.
- **APIs**: `kern_*` glue: M_* malloc→`kmalloc/kfree` (pf's malloc flags → GFP),
  `timeout()`→timer/workqueue (state purger), `rwlock_*`→RW-lock,
  `pool`→kmem_cache, epoch→RCU read side + refcounts on rulesets (matching how
  conntrack already models this), `time_second`→`ktime_get_seconds`,
  `syncache` untouched, `radix.h`/`tree.h` (BSD) vendored with Linux tweaks,
  `imsg` not needed in kernel.
- **Netlink/ioctl**: `misc_register` `/dev/pf`, all DIOC* handled exactly as
  `pf_ioctl.c` (port file with shim includes). **ioctl number encoding**: define
  `_IOR/_IOW/_IOWR(PFRDR,'n',type)` OpenBSD-style in our shared `pfvar.h` so kernel
  and Linux-`pfctl` agree bit-for-bit.
- **IPv4/IPv6**: `in_proto`/`in6_proto` glue for checksums (`ip_sum_eq`,
  pseudo-header sum via `csum_tcpudp_magic`, `tcp_input.c`'s in_cksum ported to
  `pf_compat_cksum.c`); raw socket `sosend` paths replaced by
  `icmp_send`/`icmp6_send` (RST via `tcp_v4_send_reset` equivalents kept in-pf to
  preserve PF's own return logic).
- **Interface/addresses**: `ifnet`→`net_device` + `in_ifaddr`→`net_ifa` walks via
  `net_if_FOREACH`/`in_ifa_lookup` helpers over `if_addr_list` + `fib` (replaces
  `ifa_ifwithroute` for route-to); `pf_getifaddr` ported with Linux address lists.

### 4.2 Hooking
Register netfilter hooks per family:
- `PREROUTING (NF_IP_PRI_FIRST+)`: pf input path = scrub/normalize → state lookup →
  nat/rdr (pf's own NAT, **independent of conntrack**; deployment docs warn about
  overlap zones), filter INPUT.
- `FORWARD`: filter + route-to/dup-to execution.
- `LOCAL_OUT`: filter (OUTPUT) + nat-to (locally generated).
- `POSTROUTING (pri low)`: state insert, queue classify (skb→pfq class id),
  route-to final xmit.
Verdict mapping: pass→`NF_ACCEPT`, block/normalization-drop→`NF_DROP` (with PF's
return-RST/ICMP generated by pf itself), `divert`→`NF_STOLEN` + divert channel,
`nat/rdr` rewrite → `NF_ACCEPT` after `skb_cow` + checksum fix. Coexistence with
iptables/nft: PF hooks run before conntrack (unregister-safe ordering); documented.

### 4.3 Queues (OpenBSD 8.0 `ifq`) ⇒ `pfq` qdisc shim
Port `sys/net/ifq.c/ifq.h` + `sys/net/hfsc.c` + `sys/net/fq_codel.c` (the real 8.0
queueing framework: priq glue, HFSC, FQ-CoDel as `ifq_ops`) into `module/ifq/`.
`struct ifqueue` instances are backed by a classful Linux qdisc **`pfq`**
(`register_qdisc`): `pfq_enqueue` → `ifq_enqueue` (matched class via pf tags),
`pfq_dequeue` → `ifq_deq_begin/commit` → `__dev_queue_xmit`. `tc qdisc replace dev X
root pfq` attaches; `scripts/pf-qdisc.sh` auto-attaches when a loaded ruleset
declares `queue on` (pfq handles both schedulers + default priq via `pf_qblocks`
priority bits). `DIOCADDQUEUE/DIOCGETQUEUES/DIOCGETQUEUE/DIOCGETQSTATS` (the real
8.0 names) served against the pfq/ifq registry so `pfctl -s queue -v` and
`pfctl -s ifq -v` work. Tested mandatory schedulers: **default(priq), hfsc,
fq_codel** with rate gates (§7); class visibility via `tc -s qdisc show` is a
stretch goal, counters via `pfctl` are the gate.

### 4.4 divert — real AF_DIVERT in the kernel (no netlink substitute)
OpenBSD's `divert(4)` socket family is ported as a **runtime-registered
socket family inside pf.ko**: Linux 7.3 registers protocol families with
`sock_register()` and `NPROTO = AF_MAX = 45`, so BSD's `PF_DIVERT = 32`
(the unused legacy PF_IUCV slot) fits with **zero core changes**. Implemented
in `module/pfsrc/divert_sock.c`: `socket(PF_DIVERT, SOCK_RAW, IPPROTO_DIVERT)`
+ `bind(port)` + `recvmsg/sendmsg`, packet metadata via cmsg (ifindex, dir,
divert type, original dst). `divert_packet()/divert6_packet()` (called by the
ported pf.c) queue to the bound socket; `sendmsg` re-injects with OpenBSD
semantics: `PF_DIVERT_TO` → local delivery (`netif_rx` on the recorded
interface), plain `divert` tag → re-route (`dst->output`), mirroring
`ip_divert.c:divert_output()`. The BSD `divert(8)` workflow (upstream
`regress/sys/net/pf_divert`) drives this socket unchanged; our shipped
`pdivert` is a thin Linux port. Wire format documented in
`docs/en/divert-wireformat.md`. pflog keeps its netlink multicast channel
(slot PFLOG_PROTO=1 on `NETLINK_USERSOCK`).

### 4.5 route-to / dup-to / fail-to
`pf_route6`/`pf_route` port: pool selection via `pf_lb.c` (as-is), next-hop
resolution `ifa_ifwithroute`→`pf_route4gw()`: build `flowi4/flowi6`,
`ip_route_output_key` to **gw**, then L2-redirect via `neigh_lookup(arp_tbl/nd_tbl,
&gw, dev)` + `neigh_output` (or `dev_queue_xmit` after `dev_hard_header`); TTL--
before xmit, state key = translated, `reply-to` via reverse state's stored answer
route. `dup-to` = `skb_clone` to original path. If a needed route/neigh helper
isn't exported → kernel core export patch, documented.

### 4.6 pflog (small, included)
`if_pflog.c` → netlink multicast channel (`NETLINK_PF` group) + `pflogd` userland
that writes pcap file (OpenBSD pflogd logic), so `tcpdump -r` verifies passes;
enables state/queue tests to assert headers (e.g., NAT rewrites, scrub).

### 4.7 Userland build (`tools/`)
Linux build of OpenBSD 8.0 `sbin/pfctl` (single binary — libpfctl is folded in),
`parse.y` via flex/bison, plus `pdivert` + `pflogd`, with a `compat-user/` shim
(err/warn, strlcpy, pledge stub, arc4random, sysctl subset, OpenBSD-shaped
`_IOR/_IOW/_IOWR`). Shared ABI header `include/net/pfvar.h` (ours, identical to
the kernel's) guarantees ioctl bit-parity; static asserts on every ioctl struct.

## 5. Kernel core patches
Target **zero behavior changes** to Linux; the patchset ships:
- **P1 (mandatory)** `net: export symbols needed by the PF module`
  (`neigh_output`, `ip_route_output_key`, `ip6_route_output`,
  `netlink_kernel_create`, + any added during M1–M5 builds) — additive
  `EXPORT_SYMBOL*()` lines, no logic touched. Confirmed missing in 7.3.4.
- No other core changes by default: module builds out-of-tree (`make -C
  linux-7.3.4 M=pf_port/module`), tree otherwise untouched; if a future step
  needs more (e.g., a netlink protocol reservation), each lands as its own
  `patches/kernel/000N-*.patch` appended by `scripts/apply-patches.sh`, with a
  per-patch entry in `docs/{en,pt-BR}/patches.md`: what/why/impact/how to revert.

## 6. Repo layout
```
pf_port/            (= repo root, git → pf-linux-port)
  README.md / README.pt-BR.md
  docs/{en,pt-BR}/  guide, install, apply-patches, feature-matrix, validation,
                    divert-wireformat, patches/*.md
  docs/superpowers/specs/            this file
  patches/kernel/   000N-*.patch + series + README
  module/           pf.ko + pfq.ko sources: pf/, ifq/, compat/, include/
  tools/            pfctl/, pdivert/, pflogd/, compat-user/
  scripts/          fetch-sources.sh  apply-patches.sh  unpatch.sh
                    build-module.sh   build-tools.sh    vm-create.sh
                    vm-deploy.sh      vm-run-tests.sh   bench-basic.sh
  tests/vm/         01-state.sh … 14-altq.sh, scapy/python helpers, expect-files
  _vendor/          OpenBSD 7.8/8.0 tarballs (gitignored; refetch script)
```

## 7. Validation (libvirt on the 192.168.1.242 host, Alpine) — "done" definition
`scripts/vm-create.sh` clones the Alpine 3.22 cloud qcow2 (verified: 840 MiB,
sha256 e83786b1…), NoCloud seed (user/network config), and defines **4 libvirt
domains on an isolated 192.168.122.x-free bridge lab (`pf-lan`, `pf-wan`, `pf-wan2`
networks, no NAT to the outside needed for gates)**:
`pftest-pf0` (**boots the self-built 7.3.4 kernel + initrd via direct kernel boot**;
3 NICs: lan/wan/wan2), `pftest-cli`, `pftest-srv`, `pftest-gw2`.
Decision (builder-check #1): PF stays a **single global instance bound to
`init_net`**, exactly like OpenBSD — no per-netns rulesets; hence endpoints must be
real hosts (VMs), not namespaces. The `deb` host (12 cores, libvirt 11.3 validated)
runs the lab; the laptop keeps building sources.
Suite (`vm-run-tests.sh`, ssh-driven, PASS/FAIL per feature): stateful icmp/tcp/udp
(+IPv6 smoke), block drop/return, tables (`pfctl -t -T add/del/test`),
anchors+quick, nat-to (asserted on srv: src=pf0's addr), rdr-to (srv httpd reached
via pf0 DNAT), binat-to, scrub (crafted frags via scapy on cli: overlap drop,
DF-set, max-mss clamp), **route-to pools rr + hash-source (asserted: wan vs wan2
tcpdump counts; fail-to when gw2 down; reply-to symmetry; dup-to capture)**,
**divert (`pdivert` rewrites payload end-to-end; timeout drop without server)**,
**queues: `queue on … { hfsc … }` and `fq_codel` and default priq — iperf
throughput within ±15% of rate on the bottleneck link + `pfctl -s queue -v`
counter growth + pfq attaches/detaches cleanly**, pflog capture sanity (passes
appear in pcap), ruleset reload under traffic (soak loop), rmmod/load leak check
(kmemleak + module refcounts). IPv6 policy (builder-check #4): engine is v6-complete
by construction (single pf.c); **gates are v4 for everything + v6 smoke for
state/block/nat66/scrub**; v6-only landmines (route-to6 pool failover) matrixed
"ported, smoke-validated". Performance (builder-check #2): **functional gates are
the bar; `bench-basic.sh` is informational** (reports pps vs iptables baseline, no
numeric gate) — a perf contract would force a non-faithful rewrite-first shim.
Artifacts: run log + per-test pcap attached to the Gitea release. **All gates must
pass**; anything failing ships as explicit FAIL in the feature matrix, not silently.

## 8. Milestones & delivery
M0 infra: repo+CI-ish scripts, kernel baseline build OK, VM boots custom kernel.
M1 engine-boot: compat layer + module loads, `/dev/pf`, pfctl `-f` empty + `-s info`.
M2 filter core: rules/states/tables/anchors pass VM tests 1–4.
M3 NAT suite (nat/rdr/binat) + scrub + pflog.
M4 route-to/dup-to/fail-to + divert.
M5 queues: ifq/hfsc/fq_codel port + pfq qdisc + `queue` semantics + counters.
M6 docs bilingual complete, patchset tidy, full suite re-run green, tags v0.x in repo.
Commits per milestone to Gitea (public), final release with validation artifacts.

## 9. Builder-decisions (my calls, reversible)
- Base **OpenBSD 8.0** (matches the provided userland tree exactly; ALTQ delivered
  as 8.0's real queue framework ifq{priq,hfsc,fq_codel}; older ALTQ releases are
  unrecoverable from public mirrors anyway).
- **Single global PF instance on `init_net`** (faithful to OpenBSD); validation via
  multi-VM lab, not netns (builder-check #1).
- sk_buff-backed mbuf shim instead of rewriting pf.c to skb (keeps upstream logic
  line-proximate; enables future rebase to newer OpenBSD).
- divert via netlink channel instead of inventing AF_DIVERT (no core socket-family
  patch).
- pflog via netlink not pseudo-if tap (keeps "no extra interface" promise to users).
- Module name `pf.ko` (no Linux collision).
- **No numeric perf gate** (bench informational) and **v4-gated + v6-smoked** suite
  (builder-checks #2/#4).

## 10. Risks
- `pf.c` porting surface largest; queue scheduler coverage limited to what exists
  upstream in 8.0 (mitigation: 3 schedulers gated, matrix explicit).
- ioctl-ABI bit parity between two toolchains (mitigation: single shared header,
  struct-size static asserts both sides).
- Kernel 7.3 symbol drift (mitigation: P1 export patches allowed+documented).
- Netfilter ordering vs conntrack for transit flows (mitigation: docs + test with
  conntrack module loaded/unloaded).
- Multi-segment/GSO packets through mbuf shim (mitigation: linearize-on-entry for
  v1, document perf cost; bench script quantifies).
