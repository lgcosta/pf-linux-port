# VM validation lab (libvirt / Alpine)

Português: [docs/pt-BR/vm-lab.md](../pt-BR/vm-lab.md).

All commands below were read from the scripts in `scripts/` (they are the
source of truth); the lab has been created and run during M0–M2 — a full-suite
green run is the M2+ gate and is still in progress (see §6).

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

| Gate | Scenario (asserted with real traffic, not exit codes) |
|---|---|
| `01-state.sh` | `pass in/out quick proto icmp keep state` → cli pings srv **through** pf0 (transit); pf0 then lists an icmp state via `pfctl -s states` |
| `02-block.sh` | `block drop in quick proto icmp` → cli ping fails; `block return-rst in quick proto tcp … port 8080` → cli's `nc` to srv:8080 is refused (RST), checked via nc exit |
| `03-tables.sh` | `table <blk> { 10.0.1.10 }` + `block in quick from <blk>` → `pfctl -t blk -T show` lists it; cli ping blocked; live `-T delete` unblocks the same flow |
| `04-anchors.sh` | anchor rule (`-a restricted -f`) passing icmp is evaluated **before** a later main-ruleset `block` (quick/top-placement); `pfctl -s Anchors` lists the anchor |

Planned gates per the plan document (T11–T18): 05-nat, 06-rdr, 07-binat,
08-scrub (scapy), 09-routeto, 10-dup-fail, 11-divert, 12-queues (iperf rate
tolerance ±15%), 13-pflog, 14-soak (reload under traffic + rmmod cycle +
kmemleak). IPv6 is smoke-only for state/block/nat66/scrub. `bench-basic.sh`
(planned, not in the tree yet) is informational; there is no numeric perf gate.

## 6. Current suite status (2026-11-11, honestly)

The M2 gates 01–04 exist and run, but **are not all green yet** — the data
path (netisr workqueue model) and state visibility are being fixed in the M2
gate work. The latest captured run (`out/test-logs/`) shows the mixed state:
transit ping OK / state listing failing on 01, icmp-blocked OK on 02, partial
results on 03, 04 aborted. Treat "VERIFIED-in-VM" in the
[feature matrix](feature-matrix.md) as reserved for gates that pass in a
full clean run; nothing beyond M1 is claimed VERIFIED until then.

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
