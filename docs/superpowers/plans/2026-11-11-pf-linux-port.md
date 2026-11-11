# PF-on-Linux Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a faithful OpenBSD 8.0 PF port for Linux 7.3.4 as out-of-tree modules (`pf.ko`, `pfq.ko`) + one documented core export patch + Linux `pfctl`, validated by a PASS/FAIL suite across a 5-VM Alpine libvirt lab.

**Architecture:** Ported 8.0 `sys/net/pf*.c`, `ifq/hfsc/fq_codel` files sit under an sk_buff-backed mbuf compatibility layer; netfilter hooks run PF at PREROUTING/FORWARD/OUTPUT/POSTROUTING; `/dev/pf` serves the OpenBSD ioctl ABI; queueing bridges to a classful `pfq` qdisc; divert/pflog use kernel netlink channels; userland `pfctl/pdivert/pflogd` built from 8.0 sources against a shared `pfvar.h`. See spec.

**Tech Stack:** OpenBSD 8.0 sources (C3SL mirror), Linux 7.3.4 build tree, gcc-16/bison/flex, libvirt/qemu on 192.168.1.242 ("deb"), Alpine 3.22 cloud qcow2 + cloud-init NoCloud, bash/scapy/iperf3 tests in-guest.

**Spec:** `docs/superpowers/specs/2026-11-11-pf-linux-port-design.md` (read it first; this plan argues from it).

## Global Constraints

- Kernel tree: `/home/lgcosta/lab/linux_kernel/linux-7.3.4` (Linux 7.3.4, vermagic `7.3.4`).
- PF sources base: **OpenBSD 8.0** release (`sys.tar.gz`, `src.tar.gz` from `https://openbsd.c3sl.ufpr.br/pub/OpenBSD/8.0/`, fallback `cdn.openbsd.org`), sha256-pinned in `scripts/fetch-sources.sh` (sys: `$(sha256)` — pin at T2; src: pin at T2).
- Module builds out-of-tree (`make -C <kernel> M=$PWD/module`); kernel core changes ONLY via `patches/kernel/000N-*.patch`, each documented in `docs/{en,pt-BR}/patches.md`.
- Every ported kernel file keeps the OpenBSD header block verbatim + our ` * Copyright (c) 2026 PF-on-Linux contributors (compat changes under ISC)` line.
- License of new code: ISC. Docs bilingual: EN primary + full pt-BR counterpart (`docs/en/`↔`docs/pt-BR/`, `README.md`↔`README.pt-BR.md`).
- `/dev/pf` mode 0600 root-only; all ioctl in-blobs length-validated as upstream (never relax upstream checks).
- Single global PF instance bound to `init_net` (faithful; netns = out of scope). PF hooks MUST run before conntrack (`NF_IP_PRI_CONNTRACK-1`-equivalent ordering, documented).
- Test gates are v4; v6: state/block/nat66/scrub smoke. No numeric perf gate (`bench-basic.sh` informational).
- Commit per task; branch `main`, push to `https://git.labexposed.com/lgcosta/pf-linux-port` each task.
- VM lab lives on **deb** (ssh root@192.168.1.242); never touch the laptop's `sms-vm`; laptop = build host only. Artifacts sync laptop→deb via rsync.
- pf0 VM boots the self-built kernel by direct kernel boot with virtio/ext4 **built-in** (no initrd).

## Review Focus

- Non-IPv4/IPv6 EtherTypes passing hooks unchanged (ARP etc. — expect `NF_ACCEPT`, tested).
- Fragment/extension-header packets into the mbuf shim (linearize path; crafted scapy inputs must not leak skbs or panic; tested via scrub suite).
- Multi-segment GSO/GRO skbs entering PF (must be handled: linearize fallback; tested by iperf through a passing ruleset).
- Malformed/oversized `pfioc_*` blobs from local root (length fields must be bounded-checked exactly as upstream; fuzz test T9).
- Module unload (`rmmod pf pfq`) under live traffic and with states present (must drain, no oops; tested by soak T23).
- NAT port-pool reuse after state expiry (no stale bindings; tested in T15 via rapid reconnect churn).

---

### Task 1: Repo scaffold + remote
**Files:** Create `pf_port/.gitignore` (`_vendor/`, `out/`, `*.ko`), `README.md`, `README.pt-BR.md` (project one-pager each), `LICENSE` (ISC + OpenBSD copyright notice block), `VERSIONS` (kernel 7.3.4; OpenBSD 8.0; date), `Makefile` (targets: module tools kernel-vm docs test).
**Interfaces:** none produced beyond layout in spec §6.
- [ ] Step 1: `git init` in `pf_port/`, identity lgcosta, remote origin = Gitea repo.
- [ ] Step 2: write the files above (READMEs mirror: what/why/status/features-matrix-table-empty/build-once-commands).
- [ ] Step 3: first commit, push. Verify `git ls-remote origin` shows `main`.

### Task 2: Source acquisition + vendorization
**Files:** Create `scripts/fetch-sources.sh`, `scripts/vendorize.sh`; `scripts/sha256-pin.txt`.
**Interfaces:** Produces `_vendor/openbsd/8.0-sys.tar.gz`, `8.0-src.tar.gz` (verified), and staged pristine copies at `_vendor/upstream/sys/net/*`, `_vendor/upstream/sbin/pfctl/*` used by all port tasks; prints per-file `$OpenBSD: id` lines to `_vendor/upstream/IDS.txt`.
- [ ] Step 1: fetch script with mirror→cdn fallback; verify against pin file populated from the two tarballs already downloaded + SHA256 from `.../8.0/SHA256`.
- [ ] Step 2: vendorize extracts exactly: sys: `pf.c pf_ioctl.c pf_norm.c pf_if.c pf_lb.c pf_ruleset.c pf_table.c pf_osfp.c pf_syncookies.c pfvar.h pfvar_priv.h if_pflog.c if_pflog.h ifq.c ifq.h hfsc.c hfsc.h fq_codel.c fq_codel.h ip_divert.c ip_divert.h tree.h queue.h` (sys/sys/) `radix.c radix.h` (sys/net/); src: `sbin/pfctl/*` (all .c/.h/.y/.l + Makefile), `include/`.
- [ ] Step 3: run both; `IDS.txt` non-empty; commit scripts only.

### Task 3: Kernel build pipeline (laptop)
**Files:** Create `scripts/build-kernel.sh`, `scripts/kernel-config.sh`.
**Interfaces:** Produces `out/linux-7.3.4-bzImage`, `out/System.map`, `out/.config`, module tree build-ready at `linux-7.3.4` (Module.symvers).
- [ ] Step 1: config = defconfig + `VIRTIO_BLK=y VIRTIO_PCI=y VIRTIO_NET=y EXT4_FS=y` + `NET_SCHED` `NET_EMATCH` off-safe, `KPROBES` off, `DEBUG_INFO_REDUCED` optional; script idempotent, diff-checked (`scripts/kernel-config.sh diff`).
- [ ] Step 2: full build `make -j$(nproc-2) bzImage modules`; verify `file bzImage`, `modpost` artifacts exist; record kernel `vermagic` string into `VERSIONS`.
- [ ] Step 3: commit (scripts + VERSIONS update only; out/ gitignored).

### Task 4: Core export patch + patch tooling
**Files:** Create `patches/kernel/0001-net-export-symbols-needed-by-pf.patch`, `patches/kernel/series`, `scripts/apply-patches.sh`, `scripts/unpatch.sh`; docs stubs `docs/en/patches.md`, `docs/pt-BR/patches.md`.
**Interfaces:** `apply-patches.sh <kerneldir>` → applies all series patches (git-am style `git apply`), prints per-patch rationale from `docs/*/patches.md` anchors; build tasks 3/6 consume it.
- [ ] Step 1: after M1 first build reveals the exact unexported list (start: `neigh_output`, `ip_route_output_key`, `ip6_route_output`, `netlink_kernel_create`, plus any `ip_neigh_for_gw`, `fib6_lookup_*` discovered by build/link errors), write 0001 adding `EXPORT_SYMBOL_GPL(...)` lines adjacent to each existing export cluster.
- [ ] Step 2: apply to `linux-7.3.4` (keep `git` clean via `unpatch.sh` = reverse apply; the tree is NOT a git repo → patches applied/reverted textually, never left dirty on final artifact builds without record), rebuild, verify `Module.symvers` contains them.
- [ ] Step 3: docs table entry for 0001 (what/why/impact/revert); commit.

### Task 5: VM lab provisioning on deb
**Files:** Create `scripts/vm-create.sh`, `scripts/vm-destroy.sh`, `scripts/lab-lib.sh`, `lab/{cloud-init/*.yaml.tmpl,domains/*.xml.tmpl}`.
**Interfaces:** `vm-create.sh` idempotent → domains `pftest-pf0` (3 NICs `pf-lan/pf-wan/pf-wan2` + mgmt default-net NIC), `pftest-cli`, `pftest-srv` (mgmt+data NICs), `pftest-gw2` (pf-wan2+pf-wan); static IPs per `lab/ips.env` (sourced by ALL test scripts): PF0_LAN=10.0.1.1 PF0_WAN=10.0.2.1 PF0_WAN2=10.0.3.1 CLI=10.0.1.10 SRV=10.0.2.20 GW2=10.0.3.2 GW2_WAN=10.0.2.2; ssh aliases `pf0/cli/srv/gw2` via `~/.ssh/config.d/pflab` (generated), root key auth from deb. Non-pf VMs boot Alpine stock kernel; pf0 boots direct kernel from `out/` later (T6).
- [ ] Step 1: seed ISO/dir per VM (NoCloud meta-data+user-data incl. `packages: [iperf3, tcpdump, python3, py3-scapy, bash]` — verify pkgs install needs mgmt NIC up: enable `virsh net-start default` on deb).
- [ ] Step 2: create networks XMLs (forward=none for pf-*); domains XMLs (q35, virtio disks, host passthrough? plain64 OK, 2 vCPU/1G each).
- [ ] Step 3: run; loop `ssh pf0 true` until OK (≤3min); `vm-destroy.sh` tears down cleanly both directions; commit + rsync verified.

### Task 6: Direct-boot pipeline for pf0 (custom kernel)
**Files:** Modify `scripts/vm-create.sh` (pf0 domain uses `--boot kernel=... `); Create `scripts/vm-deploy.sh` (rsync out/bzImage + module .kos + tools to deb `/var/lib/pflab/pf0/`; copies into pf0 over ssh to `/opt/pf/`).
**Interfaces:** `vm-deploy.sh pf0` leaves: `uname -r` == 7.3.4 in pf0.
- [ ] Step 1: define pf0 with direct kernel boot (`/var/lib/pflab/pf0/bzImage`, cmdline `root=/dev/vda1 ro console=ttyS0`, no initrd); serial console log capture file for tests.
- [ ] Step 2: deploy, boot, verify `uname -r`+`lsmod` clean; commit.

### Task 7: Compat layer core (`module/compat/`)
**Files:** Create `module/Makefile` (obj-m: `pf.o` `pfq.o`), `module/include/` (our `pfvar.h` staged: upstream + Linux compat block: OpenBSD-shaped `_IO/_IOR/_IOW/_IOWR`, `u_intXX_t` typedefs, `sockaddr`/`in_addr` shims), `module/compat/pfk.h` (master header), `compat/pfk_mbuf.{c,h}` (`struct mbuf{next,packet,len,cap_data,flags,type,sk_buff *skb}` — the skb wrapper + `m_copym/m_adj/m_pull/m_copydata/m_cat/m_clump/m_free/m_freem/m_dup/m_copypacket/m_devget/m_get/m_getptr` + `pfk_to_skb()/pfk_from_skb()`), `compat/pfk_kern.c` (malloc/free M_*, pools, `pfk_time` (jiffies→sec), timeout/evtimer→timer_list+work, rw/mutex wrappers, per-cpu counters, arc4random→get_random_u32), `compat/tree.h` (BSD RB via upstream with `container_of` swap), `compat/radix.{c,h}` (OpenBSD radix ported), `compat/pfk_mod.c` first skeleton: misc `/dev/pf` 0600, `DIOCGETSTATUS` only, netfilter hooks NOT yet registered.
**Interfaces (produced, used by every port task):** `struct pf_buf` ops above; `#include <net/pfvar.h>` resolves to our staged header; `pfk_ifname2dev()`, `pfk_addrofv6/v4`, per-file pattern `#ifdef PFLINUX` minimal.
- [ ] Step 1: headers + skeleton `.c` files compile clean: `make -C linux-7.3.4 M=pf_port/module` produces `pf.ko` (modpost warnings = 0).
- [ ] Step 2: deploy to pf0 VM, `insmod pf.ko` → `ls /dev/pf`, dmesg clean; commit.

### Task 8: pfctl Linux build
**Files:** Create `tools/Makefile`, `tools/compat-user/{err.c,strlcpy.c,strtonum.c,imsg-buffer.c?,pledge.c,sys-queue.h}`, stage 8.0 `sbin/pfctl/*.c|parse.y` into `tools/pfctl/` (+`pfctl_linux.c` for path/ioctl glue: `/dev/pf`, `pfctl.conf` optional), `tools/pdivert/pdivert.c` (reads pkts from NETLINK_USERSOCK proto channel: bind port=N ↔ divert id; reply write reinjects via kernel; modeled on OpenBSD `divert(8)`), `tools/pflogd/pflogd.c` (consumes NETLINK_PFLOG multicast → pcap file, rotation flags).
**Interfaces:** `tools/out/pfctl` (static-ish linked, glibc fine): supports `-n -f -s rules|states|tables|queue|info|src -t -T -sS -i -d` — same flags (same code). `pdivert -p <id>` used by test 11; `pflogd -f /var/log/pflog` used by test 08.
- [ ] Step 1: build on laptop against staged headers: `make -C tools` → flex/bison parse.y compiles without errors (only compat edits inside `#if defined(__linux__)` or small sed-style upstream changes recorded in `tools/UPSTREAM.md` diff-notes).
- [ ] Step 2: `pfctl -n -f tests/confs/min.conf` succeeds OFFLINE (parse-only).
- [ ] Step 3: deploy binary to pf0; `pfctl -s info` vs our T7 skeleton status → prints Status: Enabled; commit.

### Task 9: ioctl ABI parity harness
**Files:** Create `tools/abitest/{abitest.c,gen-sizecheck.sh}`, `tests/confs/fuzz-ioctls.py`.
**Interfaces:** `abitest` prints PASS/FAIL for every DIOC* struct: sizeof/offsetof computed by kernel (`DIOCGSTRUCTSIZE`-style custom ioctl we add under our own vendor-safe id? NO — via `DIOCGETSTATUS` roundtrip + userspace `sizeof` table generated from SAME `pfvar.h` → assert both sides) — concrete: kernel exposes `pfk_abi_hash` module param (CRC32 of packed struct sizes table); abitest recomputes from header → mismatch = FAIL. fuzz script throws oversize/garbage `pfioc_*` blobs.
- [ ] Step 1: implement, run on pf0: ABI CRC match; fuzz: no oops/leak (dmesg+kmem checks).
- [ ] Step 2: negative case: corrupt table → abitest FAILs (self-test of the harness); commit.

### Task 10: Ruleset engine port (pf_ruleset + pf_table)
**Files:** `module/pf/pf_ruleset.c`, `module/pf/pf_table.c` staged+ported; `module/pf/hooks.c` (first real netfilter registration: IPv4+IPv6 PRE/FWD/OUT/POST, verdict plumbing skeleton, pfk_mbuf ingress), `module/pf/pfk_if.c` (net_device/in_ifaddr walks replacing OpenBSD ifnet/ifa lists; interface anchor list).
**Interfaces:** `pfctl -f min.conf` with a `table <blk> { 10.0.1.99 }` + `block drop quick from <blk>` loads; `pfctl -s tables`, `pfctl -t blk -T add/test/del/kill` roundtrip; **no forwarding decisions yet** (match results not applied until T12).
- [ ] Step 1: port+compile the two files (their OpenBSD deps satisfied by T7).
- [ ] Step 2: VM: load ruleset, table ops incl. bulk add from file, `-T show`; `rmmod` after: kmemleak clean; commit.

### Task 11: pf_norm / scrub
**Files:** `module/pf/pf_norm.c` (+`pf_osfp.c` staged), `tests/vm/08-scrub.py` (scapy crafted: overlapping frags → drop, fragment TTL, `max-mss 1380`, `df-bit` set, `no-df` fragmentation, random-id reassembly correctness).
**Interfaces:** `pf_scrub()`/`pf_normalise()` wired into hooks ingress; `set reassemble yes|no` honored; OS-fp stored in states (`pass ... set os ...` matching via `pfctl -s states -v` shows osname if configured).
- [ ] Step 1: port; compile; wire.
- [ ] Step 2: run 08-scrub on lab (cli scapy → pf0 via srv sink): all asserts green incl. no skb leak; commit.

### Task 12: pf.c core (matching + states) + verdicts
**Files:** `module/pf/pf.c` staged+ported (state hash: `pfk_hashkey()` = Linux `jhash2()` over the `pf_state_key` bytes; state table = OpenBSD 8.0 open-hashing layout kept upstream-shaped via pfk wrappers — no rhashtable), `module/pf/pfk_state_timer.c` (purger→delayed_work), syncookies (`pf_syncookies.c`).
**Interfaces:** default policy pass + rules; `keep state` (icmp/tcp/udp), state insert direction (INBOUND/OUTBOUND semantics as 8.0), `block drop|return|return-rst|drop-rst` actual verdicts + return packet generation via `icmp_send`/`tcp_v4_send_reset`-like in-pf path `pfk_send_icmp_error()`; `pfctl -s states -v` full fields; `match-to state`/`sync` no-op fields fine (documented NG).
- [ ] Step 1: port pf.c in passes: (a) structs/globals, (b) `pf_test`/`pf_match_key_all`/`pf_state_insert`, (c) verdict application at hooks, (d) returns, (e) syncookies.
- [ ] Step 2: VM tests `01-state.sh` (icmp: reply accepted only with state; `block drop in` before state creation kills ping; tcp 3-way via nc; udp pseudo state; `keep state` interfaces), `02-block.sh` (return-rst payload/TCP seq asserts via tcpdump on cli), `03-anchors.sh` (include anchor quick precedence), `04-table-rules.sh` (block from table live-updated), **non-IPv4 EtherTypes (ARP frame) pass hooks untouched (tcpdump asserts ARP still served on pf0)**; all green; commit.

### Task 13: NAT (nat-to / binat-to) + pf_lb sticky/bitmap
**Files:** `module/pf/pf_lb.c` ported; NAT branches in pf.c wired at POSTROUTING (outbound) + reverse xlate on inbound; port-pool allocator using pfk (random port from range + bitmap state-attached, 8.0 semantics incl. `portrange high` bitmap persisting on rule? sticky-port yes, static-port yes).
**Interfaces:** transit `nat-to` rewrites seen on wan with correct checksums + reply xlate + state tracking; `binat-to` static 1:1; `rdr-to` (inbound DNAT incl. `route-to` combo basic); `pfctl -s states -v` shows translated keys; port exhaustion behavior = `block-rst`+ICMP as upstream? (match 8.0: no nat rule → normal block path; test asserts drop).
- [ ] Step 1: port+wire; tests `05-nat.sh` (cli→srv with SNAT: srv sees PF0_WAN; reply flows; no state → reply dropped), `06-rdr.sh` (srv httpd:80 via PF0_LAN:8080; sticky-port: 2nd client gets distinct PF0 src port? sticky: same), `07-binat.sh`; churn test rapid reconnect ×50 → no stale port reuse before timeout (Review Focus #6); commit.

### Task 14: route-to / dup-to / fail-to / reply-to
**Files:** `module/pf/pf_route.c` (new: Linux implementation of `pf_route()/pf_route6()` bodies: gw resolve `ip_route_output_key`+`neigh_lookup(&arp_tbl/nd_tbl)`+`neigh_output`, TTL-- , loop guard via `skb->mark` magic + 8.0 `pf_rt_tblid` equivalent — DECISION: loop guard = per-cpu re-entry counter like OpenBSD's `pfroute` depth check; dup via `skb_clone`+original-path re-entry), pf_lb pool selection wired (rr/round-robin-source/hash-state as upstream).
**Interfaces:** `pass out route-to (GW2 10.0.3.2) ...` → traffic lands on wan2 and gw2 delivers; pools with 2 gw rotate (rr) / hash-source stable per client; `reply-to` uses state's answer route; `dup-to` copy seen on other leg; `fail-to` when primary gw down (gw2 arp-stopped test); IPv6 route-to smoke.
- [ ] Step 1: implement + port `pf_route/pf_route6` glue; compile.
- [ ] Step 2: tests `09-routeto.sh` (tcpdump counts wan vs wan2: rr alternation exact, hash-source deterministic per src; loop-guard: pf0->pf0 route-to with gw=pf0 must not livelock: bounded pkts then block `block loop drop`? use upstream `pf_route` loop detection → drop; assert), `10-dup-fail.sh`; commit.

### Task 15: divert
**Files:** `module/pf/pf_divert_nl.c` (kernel: `netlink_kernel_create(NETLINK_USERSOCK, proto=31? use NETLINK_USERSOCK group id per divert port)`, table port→socket, `divert-to` path: copy skb → nl msg `{cookie, divport, af, ip hdr+payload}`; reinjection path on user `send`: parse → route per original semantics `ip_divert.c` reference (divert-to: re-route to original dst via gw if needed; plain divert tag: re-enter `netif_receive_skb`/local_xmit per 8.0 `pf_send` behavior; drop-after-timeout via per-packet timer default 5s? match upstream: no accept → packet never delivered (copy consumed? original held until accept/timeout then free — mirror OpenBSD: keep, timeout drop).
**Interfaces:** `pdivert -p 10000` + rule `pass in divert-to 127.0.0.1 10000`? upstream model: `divert-to` sends to divert socket at **host:port**; ours: netlink port id; document mapping in `docs/en/divert.md`; stats `DIOCBUSYSTATS` unchanged.
- [ ] Step 1: implement + tool link already from T8.
- [ ] Step 2: test `11-divert.sh`: pdivert on pf0 flips a byte of ping payload → cli receives modified echo-replies; kill pdivert → traffic stops (timeout) not leaks; commit.

### Task 16: ifq + pfq queueing
**Files:** `module/ifq/ifq.c ifq.h hfsc.c fq_codel.c` ported (skb-backed via pfk_mbuf), `module/pfq/pfq_qdisc.c` (classful qdisc `pfq`: root=ifq attach; ops: enqueue→`ifq_enqueue(ifq_for_class)`, dequeue loop; classid = queue index from pf rule queue tag), `module/pf/pf_ifq_bridge.c` (DIOCADDQUEUE/GETQUEUES/GETQUEUE/QSTATS + `pf_test` queue assignment + `set queue` default-class + ifq per-iface registry via `pfq_q_enter`), scripts `pf-qdisc.sh` auto-attach on `queue on` detection at DIOCADDQUEUE.
**Interfaces:** `pfctl -s queue -v` counters (pkts/bytes/drops per queue + per scheduler), `queue on <dev> { ... }` semantics: default priq (link share/delay), `hfsc` (realtime/upperlimit/linkshare), `fq_codel` (flows; ECN mark? upstream `ifq_fq_codel` flags: ecn,quantum). Verdict: packets matching queued rules get `skb->priority`-tag (pfq) and are served under `pfq` root qdisc.
- [ ] Step 1: port files; qdisc registers (visible `tc qdisc show` as `pfq` when attached).
- [ ] Step 2: test `12-queues.sh`: hfsc 10M upperlimit → iperf cli→srv ≈10M ±15% while unqueued passthrough ≈ line-rate; counters grow + `-s queue -v` stable; detaching leaves net clean (`tc del qdisc` + rmmod); fq_codel smoke: 1 flow flows, drops counted; commit.

### Task 17: pflog
**Files:** `module/pf/pflog_nl.c` (kernel netlink socket on `NETLINK_USERSOCK`
protocol slot `PFLOG_PROTO=1` (divert uses slot `PFDIVERT_PROTO=2` — both slots
documented in `docs/en/divert-wireformat.md`): pass/block/drop/log-rule copies per
`log`/`log all`/`block log` rules incl. `if_pflog.c` wrapper header bytes exactly as
upstream DLT_PFLOG), `tools/pflogd` from T8.
**Interfaces:** `pflogd` writes `/var/log/pflog` pcap with linktype DLT_PFLOG(117) → readable by `tcpdump -r`; test asserts NAT'd/scrub'd headers logged per rules; `pfctl -s log` config.
- [ ] Step 1: implement + wire `pf_klog` call sites in pf.c port.
- [ ] Step 2: test `13-pflog.sh` (tcpdump parse: expected pkts/ifs/priority bits present, block-with-log entries); commit.

### Task 18: Soak, leak, reload-under-traffic + bench info
**Files:** `tests/vm/14-soak.sh` (30-min: continuous iperf cli→srv + ruleset `pfctl -f` reload loop every 2s + `rmmod`/`insmod` cycle at end), `scripts/bench-basic.sh` (pps via scapy TRAFFICGEN one-pkt/sec rules, iptables-empty vs pf-empty — informational CSV only).
**Interfaces:** PASS = zero oops in dmesg, kmemleak (config DEBUG_KMEMLEAK on for test kernel build variant via `kernel-config.sh test`), conntrack-coexist run (`iptables -A ... ACCEPT; iptables -F` before each run: no rule bleed; Review Focus #5/#3: GSO path exercised by iperf).
- [ ] Step 1: build test-config kernel with kmemleak; run soak; commit artifacts.

### Task 19: Docs EN/pt-BR + release
**Files:** `docs/en/{index,install,apply-patches,build,vm-lab,pf-guide,feature-matrix,divert-wireformat,patches}.md` + full `docs/pt-BR/` counterparts; READMEs final (build-once recipe `make all`, features + matrix with **all NG/FAIL rows stated**, OpenBSD→Linux behavioral differences: no `divert(4)` sockets, queues via pfq attach, `/dev/pf` root-only, coexist notes); `Makefile` targets wired; GitHub-release notes style; git tag `v0.1.0`.
**Interfaces:** `scripts/quickstart-check.sh`: fresh clone on deb → fetch/build/deploy/run tests → exit 0 (the release gate).
- [ ] Step 1: write docs from actual artifacts (no placeholders); run quickstart-check end-to-end from clean tree on a scratch dir; tag + push + `create_release` with suite logs; commit.

---
**Execution notes:** Tasks 7→12 are the serialized critical path (same `pf.c` file; one owner). 8/9 parallelize with 7 after interfaces freeze; 13→17 follow 12; 5/6 (lab) fully parallel from the start; 19 per-milestone increments + final pass. Every ported upstream file: changes grouped with `/* PFLINUX: reason */` markers for future rebases.
