# Changelog

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
