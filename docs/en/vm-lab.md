# VM validation lab (libvirt / Alpine)

Português: [docs/pt-BR/vm-lab.md](../pt-BR/vm-lab.md).

All commands below were read from the scripts in `scripts/` (they are the
source of truth). The lab has been created and run through M0–M6; the full
suite is green (§6).

## 1. Where it lives

| Host | Role |
|---|---|
| laptop (this repo) | builds kernel/module/tools; drives everything over ssh |
| **deb** — `root@192.168.1.242` | libvirt hypervisor; runs the 4 `pftest-*` Alpine VMs |

The lab never needs NAT to the outside; the only external access is the
libvirt `default` network (192.168.122.0/24, mgmt+DHCP) used for cloud-init
package installs and test ssh. The existing `sms-vm` on deb is never touched.

## 2. Topology (single source: `lab/ips.env`)

| VM | Role | NICs | Kernel |
|---|---|---|---|
| `pftest-pf0` | filter under test (runs `pf.ko`) | `mgmt` (dhcp, default net), `lan` 10.0.1.1/24, `wan` 10.0.2.1/24, `wan2` 10.0.3.1/24 | stock Alpine via image-extracted direct boot; **or** the self-built 7.3.4 `bzImage` with `--pf0-kernel=` (`root=/dev/vda2 ro console=ttyS0,115200n8`, no initrd) |
| `pftest-cli` | client | `mgmt` dhcp (routes disabled), `lan` 10.0.1.10/24, gw 10.0.1.1 (pf0) | Alpine stock |
| `pftest-srv` | server | `mgmt` dhcp (routes disabled), `wan` 10.0.2.20/24, gw 10.0.2.1 (pf0), 10.0.3.0/24 via gw2 | Alpine stock |
| `pftest-gw2` | second-path gateway (ip_forward=1) | `wan2` 10.0.3.2/24, `wan` 10.0.2.2/24 (no mgmt NIC) | Alpine stock |

All VMs get `net.ipv4.ip_forward=1` and `rp_filter=0` via cloud-init, root
password `pftest` plus an injected lab keypair (`/root/.ssh/pflab_ed` on deb,
copy on the laptop for ProxyJump), and packages
`iperf3 tcpdump python3 scapy bash iptables` (Alpine name is `scapy`; the
image also needs `iptables` for pf0 masquerade experiments).

libvirt networks: `default` (kept as-is, started+autostarted for packages) and
isolated bridges `pf-lan` 10.0.1.0/24, `pf-wan` 10.0.2.0/24, `pf-wan2`
10.0.3.0/24 — `forward="none"`, no DHCP, plus host-side bridge IPs
`10.0.x.254` on deb so deb can ssh into every guest's data NIC. VM sizing:
2 vCPU / 1 GB each. Disks are per-VM qcow2 **backing** a shared immutable
`/var/lib/libvirt/images/alpine-base.qcow2` (synced from
`_vendor/images/alpine-3.22-x86_64-cloud.qcow2` by sha256).

> Boot note: the Alpine cloud image is not BIOS/UEFI bootable in QEMU as
> delivered (empty ESP), so *all* VMs use direct kernel boot from the image's
> own extracted kernel; `--pf0-kernel=` only swaps pf0's kernel for the
> built one.

## 3. Recreate / deploy / destroy

```sh
./scripts/vm-create.sh                       # idempotent: tears down and rebuilds pftest-* domains + seeds
./scripts/vm-create.sh --pf0-kernel=out/linux-7.3.4-bzImage   # pf0 boots the built kernel (T6)
./scripts/vm-create.sh --recreate-base       # force base-image re-sync
./scripts/vm-deploy.sh                       # laptop→deb ($DEPLOY_DIR)→pf0:/opt/pf:
                                             #   out/*bzImage, module/*.ko, tools/out/*
./scripts/vm-destroy.sh                      # domains, disks, seeds, pf-* nets, ssh fragments
./scripts/vm-destroy.sh --purge-base --purge-keys   # + base image + lab keypair
```

`vm-create.sh` ends by validating `ssh pf0 'ip -br a'` and leaves all VMs
running. Laptop ssh aliases (`pf0|cli|srv|gw2`, ProxyJump via deb) are
generated into `~/.ssh/config.d/pflab`.

**From pf0 to the other VMs.** The firewall has its own keypair and the
peers trust it, so a session on pf0 can drive the client and the server
directly — this is what the screencasts do (`ssh cli 'ping …'`):

```sh
# once per lab, from the hypervisor
ssh pf0 'ssh-keygen -t ed25519 -N "" -C pf0 -f /root/.ssh/id_ed25519'
# then authorize that public key on cli, srv and gw2
```

`/root/.ssh/config` on pf0 maps the short names `cli`, `srv` and `gw2` to
10.0.1.10, 10.0.2.20 and 10.0.3.2, and `/etc/hosts` carries the same
mapping.

## 4. Running the suite

```sh
./scripts/vm-run-tests.sh        # or: make test
```

Deterministic by design: **before every gate** the runner destroys and
restarts `pftest-pf0` (fresh disk delta + no leftover module), waits for ssh
on the mgmt DHCP address `192.168.122.8`, re-deploys `pf.ko`/`pfctl`/`*.conf`
into the guest, then runs the gate script from the **laptop** (gates talk to
the VMs through deb with nested ssh — `tests/vm/lib.sh`). Per-gate output is
captured to `out/test-logs/<gate>.sh.log`; the runner prints
`PASS <gate>` / `FAIL <gate> (rc=...)` per test, a summary `PASS=n FAIL=m`,
and exits non-zero if anything failed.

## 5. What each gate proves today

Every gate asserts **real traffic and observable state**, never just an exit
code: the packet either arrives, is dropped, or is translated, and the proof
is read back from the firewall's own counters, state table, capture files or
peer.

The full list is in §6 (16 gates, all green). Each gate is a shell script in
`tests/vm/` that talks to the VMs through deb with nested ssh, using the
helpers in `tests/vm/lib.sh` (`pf0`, `cli`, `srv`, `gw2`, `pfput`, `pfload`,
`ok`, `bad`). `scripts/vm-run-tests.sh` runs them in order and captures
`out/test-logs/<gate>.sh.log`.

Not gated (honest gaps): the IPv6 path, OS fingerprinting, `set` options
(`state-policy`, `allow-opts`) and syncookies — the code is compiled into
`pf.ko` but no gate drives it. FQ-CoDel compiles and shares its stats header
with `pfctl`, but only the **hfsc** discipline is exercised.

There is **no numeric performance gate**: the design decision (spec §7) is
that *functional gates are the bar* — a perf contract would force a
non-faithful rewrite-first shim. What exists instead is a **stress test**
(`tests/vm/16-stress.sh`, driven by `scripts/stress/run-stress.sh`) that
asserts *invariants under load* — no state/table leak, no rule-load failure,
byte-identical payload under a saturating stream, no `BUG/WARNING/Oops`, the
firewall still routing — and **reports** the throughput comparison
(baseline without the module vs. with PF enabled) as information, without a
numeric pass/fail target. Report and raw log land in
`out/stress/<timestamp>/`.

## 6. Current suite status (2026-11-12)

**16/16 gates green.** A full clean run (`./scripts/vm-run-tests.sh`, ~13 min)
recreates and reboots `pftest-pf0` before every gate, redeploys the module
and the tools, and ends with `PASS=16 FAIL=0`. The captured logs live in
`out/test-logs/<gate>.sh.log`.

| Gate | What it proves now |
|---|---|
| `01-state.sh` | `keep state` across a transit ping; the state shows up in `pfctl -ss` |
| `02-block.sh` | `block drop` kills the ping; `block return-rst` gets the client an RST |
| `03-tables.sh` | a `pfr` table blocks a host; a live `-T delete` unblocks the same flow |
| `04-anchors.sh` | an anchor rule is evaluated before a later main-ruleset `block` |
| `05-nat.sh` | source NAT: the packet leaves with the pool address, the reply is de-NATed |
| `06-rdr.sh` | port redirect end to end, plus 2 MB through it with byte-for-byte integrity |
| `07-binat.sh` | `binat-to`: the peer sees the mapped address, the original never leaks |
| `08-routeto.sh` | `route-to` leaves via the gateway's link; `dup-to` copies and keeps the flow |
| `09-scrub.sh` | `min-ttl` and `max-mss` measured on the firewall's own egress |
| `10-pools.sh` | NAT pool selection (`round-robin`, `source-hash`) |
| `11-divert.sh` | `divert-to` to the `pdivert` relay: packet to userspace and back |
| `12-pflog.sh` | logged packets reach `/dev/pflog` and land in a DLT_PFLOG pcap |
| `13-replyto.sh` | `reply-to` forces the reply onto the gateway link |
| `14-soak.sh` | ruleset reload under traffic, `rmmod` cycle, leak check |
| `15-queue.sh` | hfsc queues: attach, class statistics, a 2 MB queued flow counted by the class |
| `16-stress.sh` | stress: throughput baseline vs. PF, state/rule/table churn with leak checks, 2 MB through NAT under a saturating stream, dmesg health (run by `scripts/stress/run-stress.sh`, which writes a report) |

The screencasts ([screencast.md](screencast.md)) show most of these gates
being run, with the lab's network diagram drawn over the terminal.

## 7. Manual poking

```sh
ssh pf0                       # via laptop alias (ProxyJump deb)
insmod /opt/pf/pf.ko
/opt/pf/pfctl -e
/opt/pf/pfctl -f /tmp/g1.conf
/opt/pf/pfctl -s info ; /opt/pf/pfctl -s states -v
dmesg | grep pfk
```

Artifacts of future full runs (log + pcaps) are meant to ship with the Gitea
release (spec §7).
