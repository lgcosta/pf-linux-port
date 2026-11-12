# Caveats — divergences from OpenBSD behavior

Português: [docs/pt-BR/caveats.md](../pt-BR/caveats.md).

These are the ways this port differs from running the same ruleset on
OpenBSD 8.0, or is not yet production-shaped. Everything here is grounded in
the code (`module/pfsrc/pfk_hooks.c`, `module/pfsrc/pfk_mod.c`,
`docs/porting-notes.md`) and in lab observations; nothing is speculative.

## Data path

### 1. PF runs in process context via a workqueue (netisr model), not in the packet fast path

OpenBSD evaluates `pf_test()` on the CPU that received the packet, at
`IPL_NET`, with a non-sleeping NET read lock. Linux gives us softirq context
and *sleepable* compat locks (rw-semaphores), so the hook never calls
`pf_test()` inline: it steals the skb (`NF_STOLEN`) onto an **ordered
workqueue** (`pfk_netisr`, `WQ_MEM_RECLAIM`) and runs the engine in process
context, then re-injects (`netif_rx()` for input, `dst->output()` for
output/transit).

Consequences, all observable:

- **Verdicts are asynchronous.** Anything chained after our hook on the same
  netfilter chain (other iptables/nft rules at higher priorities) does not
  see the packet until the reinjected pass comes through. Counters and log
  interleaving differ accordingly.
- **Latency + throughput**: every packet takes a work-queue round trip
  serialized on one ordered worker. Functional gates are the current bar;
  the planned `bench-basic.sh` is informational only.
- **Reordering**: the ordered queue preserves arrival order of *queued*
  work items, but the stack around it (RPS, multi-NAPI) can still hand us
  packets whose original completion order differs from reinjection order.

### 2. Re-entry protection is a per-direction tag in `skb->mark`

The reinjected survivor carries `skb->mark |= PFK_DONE_IN (0x50460010)` or
`PFK_DONE_OUT (0x50460020)`; our hooks accept any packet already tagged for
that direction instead of re-queuing it (otherwise PF_IN → netif_rx →
PF_IN… would loop). `skb->mark` is a **shared, writable 32-bit field**: any
subsystem that *overwrites* it — nft `meta mark set`, `CONNMARK --restore`,
tc filters, some VPN stacks — can either (a) clear our tag and cause the
packet to be **re-queued through PF again**, or (b) coincidentally set those
bits and let a packet **skip** a PF pass. Practical rule on a PF-on-Linux
host: do not use fwmark mangling on the same traffic; if you must, mask the
PF tag bits out of your mark space.

### 3. No NET-lock on the data path (M2 interim)

`pf_test()` currently runs **without** the compat NET read lock (comment in
`pfk_hooks.c`): with the workqueue model, a parked control writer (ruleset
change, purge ioctl) starved the packet side badly enough to be
counter-productive. Rule-set mutation and packet walk therefore run
concurrently, protected by what upstream `pf.c` itself uses (PF_LOCK/RCU-ish
patterns) — a deliberate interim deviation being reworked with the lock
model in M5+. Until then: expect correctness under steady state, not under
aggressive concurrent reload; the reload-under-traffic soak is an open gate.

### 4. Fail-open under memory pressure

The queue item is `kmalloc(GFP_ATOMIC)` in softirq. If it fails, the hook
returns `NF_ACCEPT` — the packet **bypasses PF**. This is a deliberate
availability choice (mirrors dropping into a full queue bypassing the qdisc)
but it is a security trade-off worth knowing: PF verdicts are best-effort
exactly when the host is out of atomic memory.

### 5. Hook ordering vs conntrack differs by direction

- Ingress (`PREROUTING`, `NF_IP_PRI_FIRST`): PF runs **before** defrag and
  conntrack — as designed (spec §4.2).
- Output (`LOCAL_OUT` at `NF_IP_PRI_CONNTRACK+1`): PF sees locally generated
  traffic **after** conntrack has already committed the entry.
- Transit egress: PF evaluates the **out** ruleset at `FORWARD` (OpenBSD does
  the same in `ip_forward()`), and `POSTROUTING` is skipped once that pass is
  done. `nat-to`/`rdr-to` were validated against this ordering (gates 05/06).
- The "overlapping policy with conntrack" warning from the design still
  applies: if iptables/nft SNAT also runs, PF's translation happens at the
  layer it is hooked into, and double translation is yours to avoid.

## Control plane / lifecycle

### 6. Unload is hardened and soak-verified

`rmmod pf` deregisters `/dev/pf`, zeroes `pf_status.running` under the NET
lock, deletes **and syncs** both purge timers (they self-rearm — a real rmmod
hang was observed and fixed by stopping sources before flushing the
workqueue), unregisters hooks and flushes `pfk_netisr`. Gate `14-soak.sh`
drives ICMP+TCP traffic, checks `dmesg` for faults, then runs
`rmmod` → `insmod` → `rmmod` and compares the slab footprint before and
after: clean unload, successful reload, no growth. Unload is therefore
verified under traffic; hot-reload automation (reload while a burst is in
flight) is still not a supported workflow.

## Behavioral notes verified today

### 7. `pfctl -s states` lists real states, including translations

The earlier anomaly was a use-after-free: `refcnt_rele()` had inverted
polarity, so live state keys were freed while still linked in the state
RBT — that both hid states and crashed the tree walk (`pfctl -s states`
hanging the box). After the fix, gates 01/04/05/06/07 assert on the listed
states, and translated flows print both tuples (`10.0.2.1:59335
(10.0.1.10:4380) -> 10.0.2.20:8`).

### 8. Consumers still required for two features

`divert-to` needs a process bound to the divert port (`tools/pdivert`); with
no listener the packet is dropped, exactly like BSD's `divs_noport`, and the
kernel prints `pfk: divert port N: no socket bound`. `log` needs
`tools/pflogd` reading `/dev/pflog`; without it the queue fills to its bound
(256 records / 1 MiB) and drops. Neither is a stub any more, but neither does
anything until its consumer runs (see [pf-guide.md](pf-guide.md)).

### 9. Queue configuration hits a lock assertion in the purge path (open)

Loading a `queue` rule now reaches the scheduler (`pfq_alloc` succeeds, the
hfsc discipline is created), and `pf_commit_queues()` is exercised end to end.
Two bugs were found and fixed while getting there — the root-class handle
colliding with odd pf queue ids (`pfq_addqueue` returned EBUSY), and a NULL
discipline not being checked.

What still blocks queues: the port's purge timer can run while an ioctl holds
the write lock, and `pf_purge_expired_states()` asserts the opposite:

```
splassert: pf_purge_expired_states: expected 0, got 2
kernel BUG at compat/pfk_kern.c:51
```

OpenBSD serialises this with `splnet()`; the port replaced the spl layer with
rw-semas and a work queue, so the assertion is now a real race rather than a
formality. Until it is fixed (release the lock around the call, or give the
state tree its own lock as upstream does for `pf_state_lock`), a host that
configures queues can wedge the module in `Unloading`. **Do not enable
`queue` rules on a live host yet** — the 14 lab gates never configure queues,
which is why they stay green.

## Compat-layer deviations (from `docs/porting-notes.md`, M1 record)

These are deliberate, marked-in-code divergences of the compat layer; they
matter when reasoning about rule semantics:

- **Socket-owner matching** (`user`/`pid` on rules): uid only — Linux `struct
  sock` carries no egid/cpid, so gid degrades to `(gid_t)-1`, pid to
  `NO_PID`.
- **Checksums**: driver offload is not trusted on ingress (M1 decision) —
  TCP checksums are recomputed in software; output pseudo-checksums are done
  in the port's own path.
- **IP options / record-route / timestamps**: not processed on the PF output
  path; **ICMP redirects are not generated** by PF's route code (the Linux
  FIB emits those).
- **Routing labels**: non-goal — the ioctl layer rejects labeled rules;
  `rtable_iterate()` always returns NULL (no multipath uRPF iteration).
- **Interface groups**: Linux has none; `group …` matches behave like a
  missing group on OpenBSD.
- **GSO/multi-segment skbs**: handled by linearization on entry to the mbuf
  shim (perf cost documented in spec §10; gates will quantify).
- **Single instance**: one PF world in `init_net`, like OpenBSD has one
  kernel. Containers/namespaces are not visible to PF and cannot be filtered
  with it in this design.

## What is *not* a caveat

ARP and other non-IPv4/IPv6 EtherTypes never enter the netfilter IPv4/IPv6
hooks — they pass through the box untouched, exactly as PF on OpenBSD ignores
non-IP. `/dev/pf` is 0600 root-only by design (matches OpenBSD's
`securelevel`-era default for `pfctl` write access being root).
