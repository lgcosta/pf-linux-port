# PF on Linux — implementing it from scratch on Debian 13 (trixie)

This document is the complete path, from zero, to run **PF-on-Linux** (the
OpenBSD 8.0 PF ported to Linux as an out-of-tree kernel module) on a
**Debian 13 (trixie)** machine: prepare the host, build the 7.3.4 kernel,
build `pf.ko` and `pfctl`, bring up the libvirt lab, run the 16-gate suite
and the **stress test**.

Every command below was executed on the lab's Debian 13.7 (`trixie`) host and
the real output is shown. Where a number matters, it is the measured number —
not an estimate.

---

## 0. What has to be clear before you start

| Fact | Practical consequence |
|---|---|
| `pf.ko` is built against the **Linux 7.3.4** tree and carries that kernel's `vermagic` | It does **not** load on the Debian 13 kernel (6.12). Do not try `insmod pf.ko` on 6.12: the `vermagic` differs and the load is refused. |
| The module registers **netfilter** hooks in `init_net` (IPv4 and IPv6, 5 points each) | PF coexists with `nftables`/`iptables`; it does not replace the Debian firewall. |
| PF runs in **one network namespace only** (`init_net`) | There is no per-netns PF instance. |
| The OpenBSD `pfctl` talks to the module through the `/dev/pf` ioctl ABI | The `pfctl` in this tree is **OpenBSD 8.0** compiled for Linux, not Debian's. |

Three roles fit on one Debian 13 machine — and that is how the lab was
validated:

| Role | What it does | Where |
|---|---|---|
| **build host** | builds the 7.3.4 kernel, `pf.ko` and the userland | `~/lab/linux_kernel/pf_port` |
| **hypervisor** | hosts the 4 test VMs (libvirt/KVM) | same host, `libvirtd` |
| **PF host** (optional) | runs PF on Debian 13 itself, after booting the 7.3.4 kernel | same host — see the warning in §9 |

---

## 1. Step 1 — prepare the host

```sh
git clone <repo-url> ~/lab/linux_kernel/pf_port   # or: cp -a /path/pf_port ~/lab/linux_kernel/
cd ~/lab/linux_kernel/pf_port
sudo ./scripts/debian13/prepare-host.sh
```

The script is idempotent: it installs the packages and prints the toolchain
state. Real output on Debian 13.7:

```
== toolchain
gcc          14
make         GNU Make 4.4.1
bison        bison (GNU Bison) 3.8.2
flex         flex 2.6.4
bc           bc 1.07.1
rsync        rsync  version 3.5.0  protocol version 32
libvirt      11.3.0
qemu         QEMU emulator version 10.0.13 (Debian 1:10.0.13+ds-0+deb13u1)
geniso       genisoimage 1.1.11 (Linux)

PREPARE_HOST_OK
```

**About the compiler.** Debian 13 ships `gcc-14` as the default and has **no
`gcc-15` in trixie** (`apt-cache policy gcc-15` → no candidate). The
`docs/en/install.md` asks for "gcc ≥ 15" because the original development
host is Fedora. On Debian 13 the build uses **gcc 14.2.0** — the 7.3.4 kernel
and `pf.ko` compile without a patch (verified, §4).

To split the roles, use `--build-only` or `--lab-only`.

---

## 2. Step 2 — get the sources

### 2.1 The 7.3.4 kernel tree

The repository does **not** ship the kernel: it expects the tree at
`~/lab/linux_kernel/linux-7.3.4` (sibling of the repository). From zero:

```sh
cd ~/lab/linux_kernel
curl -fLO https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-7.3.4.tar.xz
curl -fLO https://cdn.kernel.org/pub/linux/kernel/v7.x/sha256sums.asc
sha256sum -c --ignore-missing sha256sums.asc     # checks only the tarball you fetched
tar xf linux-7.3.4.tar.xz
```

The hash matches what kernel.org publishes:

```
b11b6df8295e8e991ff040b71aaa4994a8b55caa23b6b092907f195b5840fd1f  linux-7.3.4.tar.xz
```

### 2.2 The OpenBSD 8.0 sources (PF itself)

```sh
cd ~/lab/linux_kernel/pf_port
./scripts/fetch-sources.sh     # fetches sys.tar.gz + src.tar.gz, verifies sha256 (pins in VERSIONS)
./scripts/vendorize.sh         # extracts only the ported files -> _vendor/upstream + IDS.txt
```

Wait for the `OK   sys.tar.gz` and `OK   src.tar.gz` lines. `fetch-sources.sh`
tries the `openbsd.c3sl.ufpr.br` mirror first and falls back to
`cdn.openbsd.org`.

---

## 3. Step 3 — build the 7.3.4 kernel

```sh
cd ~/lab/linux_kernel/pf_port
KERNELDIR=~/lab/linux_kernel/linux-7.3.4 ./scripts/build-kernel.sh
```

What the script does, in order:

1. `scripts/kernel-config.sh` — starts from `make defconfig` and enables
   `VIRTIO_BLK/PCI/NET=y`, `EXT4_FS=y`, `NETFILTER=y`, `NET_SCHED=y`,
   `INET=y`, `IPV6=y`, `SERIAL_8250(+CONSOLE)=y` (this is what lets the `pf0`
   VM boot the kernel directly, **with no initrd**) and runs
   `make olddefconfig`;
2. `make -j$(nproc-2) bzImage modules_prepare` — compiles the kernel and
   prepares the tree for out-of-tree modules;
3. copies `vmlinux.symvers` → `Module.symvers` (kbuild does not write the
   former) and makes sure `scripts/module.lds` exists;
4. publishes into `out/`: `linux-7.3.4-bzImage`, `System.map`, `.config`,
   `kernelversion`, `bzImage.sha256`.

It ends with:

```
KERNEL_BUILD_OK 7.3.4
```

Reference time on the lab host (Debian 13, 12 threads): **~4 min** for the
kernel build itself (`bzImage` + `modules_prepare`) on a tree that already
carried objects, and the whole script — configure, build, publish the
artifacts — in under 3 minutes. A tree straight from the tarball starts
empty; expect the `make` step to dominate there.

---

## 4. Step 4 — build the `pf.ko` module

```sh
cd ~/lab/linux_kernel/pf_port
./scripts/build-module.sh          # == make -C $KERNELDIR M=$PWD/module
```

Output: `MODULE_BUILD_OK` and the artifact `module/pf.ko`.

**Check the `vermagic`** — it decides where the module can load:

```sh
modinfo module/pf.ko | grep vermagic
# vermagic:       7.3.4 SMP preempt mod_unload
```

This is why kernel and module must come from the **same** tree: a `pf.ko`
built against another 7.3.4 tree can have a subtly different `vermagic`
(`SMP preempt mod_unload modversions` plus the build's `CONFIG_*`) and the
load is refused.

### 4.1 Userspace ↔ kernel ABI check

`pfctl` talks to the kernel through shared structs. The `abitest` harness
compares the layout on both sides:

```sh
make -C tools abi      # builds abitest (userspace) and abitest-module (kernel),
                       # dumps both size/offset tables and diffs them
```

No diff output = the ABI is intact.

---

## 5. Step 5 — build the userland

```sh
make -C tools          # -> tools/out/{pfctl,pdivert,pflogd} + abitest
```

`pfctl` is produced **statically** (`-static`) so it can be copied into the
Alpine VMs without dependencies. Offline check (needs no `/dev/pf`):

```sh
(cd tools && ./out/pfctl -nf tests/01_basic.conf)    # parse-only: no output = ok
make -C tools test                                   # runs every test .conf
make -C tools abi                                    # userspace vs kernel ABI tables
```

---

### 5.1 What the Debian 13 build produced (verified)

Everything below was built and run on Debian 13.7 with **gcc 14.2.0** — no
patch, no backport:

```
$ ./scripts/build-module.sh
...
MODULE_BUILD_OK
$ modinfo module/pf.ko | grep vermagic
vermagic:       7.3.4 SMP preempt mod_unload
$ make -C tools test
PASS tests/01_basic.conf (exit 0)
PASS tests/02_translations.conf (exit 0)
PASS tests/03_queues_scrub.conf (exit 0)
PASS tests/broken.conf rejected (non-zero exit)
$ make -C tools abi
ABI OK: identical tables
```

The `vermagic` printed by the Debian build is identical to the one the lab
VM accepts. Verified end to end: the module built on Debian 13 was copied
into the lab VM and loaded there —

```
INSMOD_DEBIAN_MODULE_OK
Status: Enabled
crw-------    1 root     root       10, 259 /dev/pf
[  119.057040] pfk: netfilter hooks registered (10, netisr wq data path)
```

— as long as it was built from the same tree as the kernel the VM boots.

---

## 6. Step 6 — the libvirt lab on Debian 13

The lab is 4 Alpine VMs (`pf0` = the firewall under test, plus `cli`, `srv`,
`gw2`), a DHCP management network and three isolated bridges
`pf-lan`/`pf-wan`/`pf-wan2`. The topology lives in `lab/ips.env`.

```sh
cd ~/lab/linux_kernel/pf_port
./scripts/vm-create.sh --pf0-kernel=out/linux-7.3.4-bzImage
```

The command is idempotent: it tears down and recreates the `pftest-*`
domains, generates the cloud-init seeds, syncs the shared Alpine base image
and starts everything. Without `--pf0-kernel=`, `pf0` boots the kernel from
its own Alpine image — and then `pf.ko` **does not load** (vermagic). Always
pass `--pf0-kernel=` when the intent is to test PF.

Deploy the artifacts into the VM:

```sh
./scripts/vm-deploy.sh       # build host -> deb ($DEPLOY_DIR) -> pf0:/opt/pf/
```

Check the VM:

```sh
ssh pf0 'uname -r; lsmod | grep -w pf; ls -l /opt/pf/'
```

Expected: `7.3.4`, the `pf` module loaded, and `pf.ko`/`pfctl`/`pdivert`/
`pflogd` in `/opt/pf/`.

---

## 7. Step 7 — the 17-gate suite

```sh
./scripts/vm-run-tests.sh          # or: make test
```

The runner restarts `pf0` before every gate (clean state), re-deploys the
module and the tools, and runs the gate from the build host. Expected output:

```
PASS=17 FAIL=0
```

Per-gate logs land in `out/test-logs/<gate>.sh.log`.

---

## 8. Step 8 — the stress test

The project had no performance gate (`docs/en/vm-lab.md` recorded that gap
explicitly). The stress test is a new gate, `tests/vm/16-stress.sh`, which
asks different questions from the functional gates: *"does the engine stay
correct, bounded and leak-free while traffic, states, rules and tables
churn?"*

```sh
./scripts/stress/run-stress.sh                 # default profile
./scripts/stress/run-stress.sh --quick         # short profile (smoke)
./scripts/stress/run-stress.sh --seconds 30 --connections 2000 --reloads 500
```

Options: `--seconds`, `--parallel`, `--rate`, `--connections`, `--reloads`,
`--rules`, `--table-entries`, `--max-overhead`. The runner resets `pf0`,
deploys, runs the gate and writes `out/stress/<timestamp>/report.md`.

> **The traffic phases run at a bounded rate** (`--rate`, default 100 Mbit/s
> per stream = 400 Mbit/s total). This is not a convenience: PF steals every
> skb onto one ordered workqueue and runs the engine in process context, so the
> backlog — and the memory it takes — grows with the offered rate. On the
> 1 GB lab guest, 400 Mbit/s is stable and **~1 Gbit/s OOM-panics the
> kernel** ("System is deadlocked on memory"). The stress test therefore
> works in the regime the port survives and reports the ceiling separately
> instead of killing the firewall under test. Details in
> [`caveats.md` §13](caveats.md).

### 8.1 What each phase measures

| Phase | What it does | How it is judged |
|---|---|---|
| 0 — baseline | throughput `cli -> srv` at the bounded rate with the **module out of the path** (`rmmod pf`) | must be > 0 Mbit/s (otherwise the rest is meaningless) |
| 1 — overhead | the same traffic with PF enabled and stateful rules | overhead % = (baseline − with PF) / baseline, against the limit (30% by default) — at the capped rate this says "PF sustains the offered rate" |
| 2 — state churn | 300 concurrent TCP connections to a dedicated listener on the server | states **appear** in the table (`pfctl -s states`) and **vanish** after `pfctl -sr` → no leak |
| 3 — rule churn | 200 reloads of a 200-rule ruleset | zero load failures, reload time measured **inside** the VM (min/avg/max), rule count stable |
| 4 — table churn | 2000 entries in a `pfr` table | entries are gone after the flush → no table leak |
| 5 — integrity | 2 MB crossing `nat-to` **while** a stream saturates the link | sha256 of the received file = sha256 of the source |
| 6 — health | `dmesg`, PF counters, and a final ping | no `BUG/WARNING/Oops`; counters move; the firewall still routes |

### 8.2 Reading the report

A real run on the lab (full profile, `out/stress/<timestamp>/report.md`):

```
STRESS phase=baseline baseline_mbit=400 seconds=10 parallel=4 rate_per_stream=100M
STRESS phase=overhead pf_mbit=400 overhead_pct=0 limit_pct=30 rate_per_stream=100M
STRESS phase=state_churn connections=300 states_before=0 states_created=300 states_after_reset=0 established_on_srv=214 rules_loaded=3 pf_status=Enabled
STRESS phase=rule_churn reloads=200 rules_per_load=200 reload_fails=0 reload_ms_avg=11 reload_ms_max=20 rules_final=202
STRESS phase=table_churn entries_requested=2000 entries_added=2000 add_errors=0 entries_after_flush=0
STRESS phase=integrity bytes=2000000 sha256_match=yes
STRESS phase=health dmesg_anomalies=0 match=5855 state_entries=7
```

Reading it: PF sustains the capped 400 Mbit/s with **0% overhead** against the
no-module baseline, tracks **300/300** concurrent flows as states and drains
the table back to 0, reloads a 202-rule ruleset **200 times** with no failure
(11 ms average, 20 ms worst), flushes **2000** table entries back to 0, moves
2 MB through NAT byte-identically while a stream runs, and leaves no
`BUG/WARNING/Oops` behind.

The runner prints `STRESS RESULT: PASS` or `FAIL` and exits with the gate's
status.

> **Honesty note:** the throughput numbers come from a virtualized lab (KVM,
> 2 vCPU per VM, `virtio` NICs bridged on the host). They are meant to
> *compare* baseline against PF in the same environment, not to predict
> production. The measured overhead includes the packet copy and PF's
> software path — the price of a stateful filter.

---

## 9. Step 9 (optional) — running PF on Debian 13 itself

This is **not** required for the lab, and it has a cost: the lab host is also
the hypervisor — rebooting into another kernel takes the VMs down.

```sh
cd ~/lab/linux_kernel/linux-7.3.4
sudo make modules_install                       # pf.ko -> /lib/modules/7.3.4/
sudo cp arch/x86/boot/bzImage /boot/vmlinuz-7.3.4
sudo update-initramfs -u -k 7.3.4               # or: update-grub
sudo reboot                                     # ⚠ takes the lab VMs down
```

After the boot (with 7.3.4 active):

```sh
uname -r                                        # 7.3.4
sudo insmod /lib/modules/7.3.4/pf.ko            # creates /dev/pf (0600) and registers the hooks
ls -l /dev/pf
sudo cp ~/lab/linux_kernel/pf_port/tools/out/pfctl /usr/local/sbin/
sudo pfctl -e
sudo pfctl -f /etc/pf.conf
sudo pfctl -s info
```

A minimal `/etc/pf.conf` (OpenBSD 8.0 syntax):

```
set skip on lo
block return
pass on egress
```

To keep PF up across reboots, a simple unit:

```ini
# /etc/systemd/system/pf.service
[Unit]
Description=PF-on-Linux engine
After=network-pre.target
Before=multi-user.target

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/sbin/modprobe pf
ExecStart=/usr/local/sbin/pfctl -e
ExecStop=/usr/local/sbin/pfctl -d
ExecStop=/sbin/modprobe -r pf

[Install]
WantedBy=multi-user.target
```

And `pflogd` (writes a `DLT_PFLOG` pcap with the OpenBSD `pflog(4)` record
layout):

```ini
# /etc/systemd/system/pflogd.service
[Unit]
Description=PF log daemon
After=pf.service

[Service]
ExecStart=/usr/local/sbin/pflogd /var/log/pf/pflog.pcap

[Install]
WantedBy=multi-user.target
```

> `pflogd` reads `/dev/pflog` (created by the module, 0600). This `pfctl`
> has **no** `-s pflog` modifier: logging is turned on per rule with the
> OpenBSD keyword (`pass log on egress ...`, `block log ...`). The on-disk
> pcap lives wherever you point `pflogd`.

---

## 10. Verification checklist

```sh
# 1. module loaded and device created
lsmod | grep -w pf && ls -l /dev/pf

# 2. PF enabled
pfctl -s info | head -2                     # Status: Enabled

# 3. the OpenBSD 8.0 parser in the Debian binary
pfctl -nf /etc/pf.conf

# 4. hooks registered
dmesg | grep pfk                           # "pfk: netfilter hooks registered (10, netisr wq data path)"

# 5. the firewall really filters
pfctl -sr; pfctl -s states

# 6. the full suite
./scripts/vm-run-tests.sh                  # PASS=17 FAIL=0

# 7. stress
./scripts/stress/run-stress.sh             # STRESS RESULT: PASS
```

---

## 11. Troubleshooting

| Symptom | Cause | Action |
|---|---|---|
| `insmod: ERROR: could not insert 'pf.ko': Invalid module format` | `vermagic` differs from the running kernel | kernel and module must come from the **same** tree; rebuild both |
| `insmod` fails on the Debian kernel (6.12) | the module targets 7.3.4 | boot the 7.3.4 kernel (§9) or test inside the lab VM |
| `pfctl: Permission denied` | `/dev/pf` is 0600 root | run as root |
| `pfctl: Device not configured` | module loaded but `pfctl -e` never ran | `sudo pfctl -e` |
| `virsh: cannot connect to qemu:///system` | libvirt is down | `sudo systemctl enable --now libvirtd` |
| `Cannot find device 'eth2'` | interface names changed | resolve with `ip route get <ip> \| grep -o 'dev [a-z0-9]*'` (gate 15 does this) |
| a gate fails right after the deploy | `pf.ko` was not copied | the runner checks `test -s /opt/pf/pf.ko`; see `out/test-logs/<gate>.sh.log` |
| stress: `baseline throughput unavailable` | `iperf3` is not running on the server | `srv 'pkill -x iperf3; iperf3 -s -D'` and check `netstat -lnt \| grep 5201` |
| stress: `state churn` fails | the guest's `bash` has no `/dev/tcp` | the gate falls back to `nc`; check `bash --version` in the VM |
| the guest reboots during a stress run | the offered rate exhausted the guest's memory (workqueue backlog) | lower `--rate`; see [caveats.md §13](caveats.md) — the ceiling measured on the lab is ~1 Gbit/s on a 1 GB guest |
| `gcc-15` not found | it does not exist in trixie | use the default gcc 14 (§1) |

---

## 12. Known limits (read before using this in production)

- This port is **not** OpenBSD PF: the deviations are documented in
  [`docs/en/caveats.md`](caveats.md) and
  [`docs/en/feature-matrix.md`](feature-matrix.md).
- No `pfsync`/CARP, IPsec/pfkey, `ftp-proxy`, per-netns PF, and none of the
  `cbq`/`wfq`/`red` schedulers (removed upstream before OpenBSD 8.0).
- PF registers netfilter hooks in `init_net` — on a host that already runs
  `nftables`, traffic traverses both. PF's `bypass` only applies to traffic
  entering through PF's own hook.
- IPv6, OS fingerprinting, `set state-policy`/`allow-opts` and syncookies
  compile but are **not** covered by a gate (known gap, §5 of
  [`docs/en/vm-lab.md`](vm-lab.md)).

---

## 13. References

| Document | Contents |
|---|---|
| [`docs/en/install.md`](install.md) | the generic build recipe (without the Debian walk-through) |
| [`docs/en/vm-lab.md`](vm-lab.md) | the lab, topology and gates |
| [`docs/en/feature-matrix.md`](feature-matrix.md) | what is verified and what is not |
| [`docs/en/caveats.md`](caveats.md) | known deviations from OpenBSD 8.0 |
| [`docs/en/pf-guide.md`](pf-guide.md) | day-to-day rulesets and warnings |
| Portuguese version | [`docs/pt-BR/debian13.md`](../pt-BR/debian13.md) |
