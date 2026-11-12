# Architecture guide — one page

Português: [docs/pt-BR/guide.md](../pt-BR/guide.md).

## What this port is

A faithful port of the **OpenBSD 8.0 PF engine** running inside **Linux
7.3.4** as one out-of-tree module, `pf.ko`. Nothing is faked: the matching,
state, table, normalization and queueing code *is* OpenBSD's, compiled under a
compatibility layer; control comes from the real `pfctl` speaking the real
OpenBSD ioctl ABI through `/dev/pf`. There is **one global PF instance bound
to `init_net`** — exactly like OpenBSD has one kernel; per-netns instances are
out of scope.

Build targets: OpenBSD 8.0 sources (sha256-pinned, `VERSIONS`), Linux 7.3.4
(`linux-7.3.4`), `gcc >= 15`, bison. `pfvar.h` v1.548 is shared bit-for-bit
between kernel and userland (ioctl encoding per `module/include/pfk.h`).

## Components

| Path | Role |
|---|---|
| `module/pfsrc/` | Ported OpenBSD files: `pf.c` (engine), `pf_ioctl.c` (DIOC* ABI), `pf_norm.c` (scrub/fragments), `pf_table.c`, `pf_ruleset.c`, `pf_if.c`, `pf_lb.c` (pools/NAT ports), `pf_osfp.c`, `pf_syncookies.c`, `ifq.c`/`hfsc.c`/`fq_codel.c` (staged for M5), `if_pflog.c` + `ip_divert.c` (staged references), `divert_sock.c` (divert entry stub), `pfk_mod.c` (load/exit, `/dev/pf`), `pfk_hooks.c` (netfilter data path) |
| `module/compat/` | Linux plumbing under the BSD-shaped sources: mbuf-over-`sk_buff` shim (`pfk_mbuf`), kernel glue (malloc/pools/timeouts/locks, `pfk_kern`), interface+address views (`pfk_if`), BSD-semantics IP output/forward (`pfk_ip4`, `pfk_ip6`), socket lookups (`pfk_pcb`), ifq API with priq semantics (`pfk_ifq`), radix/route-table/crypto ports |
| `module/include/` | `pfk.h` (master compat header + OpenBSD-style `_IO*` encoding), staged `net/pfvar.h`, `pfk_mbuf.h`, `pfk_net.h`, `pfk_if.h` |
| `tools/` | Linux build of OpenBSD 8.0 `pfctl` (single binary), `pdivert` relay, ABI parity harness (`abitest` + `make abi`), offline parse tests (`make test`). Deltas vs upstream: `tools/UPSTREAM.md` |
| `scripts/` | `fetch-sources.sh`, `vendorize.sh`, `stage-port.sh` (deterministic staging), `build-kernel.sh`, `build-module.sh`, `vm-*.sh` lab automation |
| `tests/vm/` | VM suite gates `01-state.sh … 04-anchors.sh` + `lib.sh`; `tests/confs/` rulesets deployed to the lab |
| `patches/kernel/` | Core patch set — currently **empty** (`p1-needs.txt` records why no exports were needed); see [patches.md](patches.md) |

## Data path (M2, as implemented today)

OpenBSD runs PF at `IPL_NET` in process-ish context; our compat locks are
sleepable, so the hook **never calls `pf_test()` in softirq**. Packets are
stolen onto an ordered workqueue (`pfk_netisr`) and the verdict is applied by
re-injection, tagged per direction via `skb->mark` so the survivor is not
re-queued by our own hook.

```
                        Linux 7.3.4, module pf.ko
  NIC ──► netfilter            (softirq — enqueue only)
        PRE_ROUTING  PF_IN ─┐                    ┌─ LOCAL_OUT   PF_OUT
        POST_ROUTING PF_OUT ─┤  NF_STOLEN        ├─ (runs AFTER conntrack)
        LOCAL_IN/FORWARD: pass-through           ┘   POST_ROUTING: NF_IP_PRI_LAST-1
                              │  (skb ref + kmalloc pfk_pkt;
                              │   alloc failure ⇒ NF_ACCEPT = fail-open)
                              ▼
                  pfk_netisr workqueue  (process context)
                              │
                  pfk_m_wrap_skb(skb)──► struct mbuf shim
                              │
                          pf_test(af, dir, ifp, &m)   ◄── the real OpenBSD 8.0 engine:
                              │                          scrub/normalize → state lookup
                              │                          → rules/anchors/tables/quick
                              │                          → state insert → verdict
                              ▼
        pass ──► mark |= PFK_DONE_IN/OUT ──► reinject:  PF_IN: netif_rx() (survivor
                                                      of reassembly allowed through)
                                                  PF_OUT: skb_dst()->output()
        block/drop ──► kfree_skb()  (+ PF's own return-rst/ICMP generation)
        fragment-buffered ──► our reference dropped; pf keeps the survivor (m == NULL)
```

Control plane: `/dev/pf` (miscdev, mode 0600, write gated on `CAP_NET_ADMIN`)
→ `pfk_ioctl_io()` copies the parameter struct sized from the OpenBSD `_IOR/
_IOW` encoding, dispatches into the ported `pfioctl()` (`pf_ioctl.c`) — the
full DIOC* ABI as upstream. `pfctl -e/-f/-s/-t` are that same OpenBSD binary,
rebuilt.

## Queues, divert, pflog — today's observable behavior

- **Queues**: `pfctl` parses `queue`/`queue on` (8.0 syntax); kernel-side the
  ifq API is implemented by `compat/pfk_ifq.c` with **priq semantics only**;
  the `pfq` classful qdisc and the HFSC/FQ-CoDel back ends land in **M5**.
  `ifq.c`/`hfsc.c`/`fq_codel.c` are staged and compile-ready but are not in
  the module object list yet.
- **Divert**: `divert-to`/`divert` tag evaluate to PF's `PF_DIVERT` verdict;
  `divert_packet()` (`pfsrc/divert_sock.c`) currently **drops the packet with
  a rate-limited kernel log** because the AF_DIVERT socket family is not yet
  instantiated. The `pdivert` relay (userspace) exists and expects
  `socket(PF_DIVERT=32, SOCK_RAW, IPPROTO_DIVERT=254)`.
- **pflog**: `pflog_packet()` returns success but **logs nothing** (one-time
  `pr_warn "packet not logged"`); `if_pflog.c` is staged, netlink back end is
  M4/M5.

## Milestones (see feature-matrix.md for per-feature status)

| Milestone | Scope | Status (2026-11-11) |
|---|---|---|
| M0 | repo, scripts, kernel build, VM boots custom kernel | done |
| M1 | compat layer, `pf.ko` loads, `/dev/pf`, `pfctl -f`/`-s info` | done |
| M2 | filter core: rules/states/tables/anchors VM gates | **done** — gates 01–04 green |
| M3 | NAT (`nat-to`/`rdr-to`/`binat-to`), scrub, pflog | **done** — gates 05/06/07/09 (incl. 2 MB integrity through a redirect) and pflog gate 12 |
| M4 | route-to/dup-to/reply-to, pools, divert e2e | **done** — gates 08/10/13 and divert gate 11 |
| M5 | queues: ifq back ends (hfsc/fq_codel) | **partial** — back ends compiled into `pf.ko`; queue attachment/pacing pending |
| M6 | docs complete, patchset tidy, full suite green, tagged releases | in progress — docs EN+PT-BR complete, 14/14 suite green, tagged |

## Next reads

[build.md](build.md) → [vm-lab.md](vm-lab.md) → [pf-guide.md](pf-guide.md) →
[feature-matrix.md](feature-matrix.md) → [caveats.md](caveats.md).
