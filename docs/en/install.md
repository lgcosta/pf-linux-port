# Applying the PF-on-Linux patch set / build guide

Both languages: this file is mirrored at `docs/pt-BR/install.md`.

Target: Linux **7.3.4** source tree + out-of-tree modules `pf.ko`
(PF engine, divert(4) family, ifq queues). The port is currently delivered
as a **source tree + zero core patches** (see patches/kernel/README.md):
the module links against symbols the stock 7.3.4 kernel already exports.

## 1. Prerequisites

| Requirement | Why |
|---|---|
| gcc >= 15, GNU make, flex, bison, bc | kernel + pfctl build |
| `glibc-static` (or musl-static) | static pfctl for guest VMs |
| Linux 7.3.4 sources (`/home/lgcosta/lab/linux_kernel/linux-7.3.4`) | build target |
| OpenBSD 8.0 `sys.tar.gz` + `src.tar.gz` | ported sources (fetched + sha256-verified by script; primary mirror openbsd.c3sl.ufpr.br, fallback cdn.openbsd.org) |
| root on the build host | module signing not configured; `insmod` as root |

## 2. Steps

```sh
cd pf_port

# 2.1 fetch + verify OpenBSD 8.0 sources (checksums pinned in VERSIONS)
./scripts/fetch-sources.sh
./scripts/vendorize.sh          # stages _vendor/upstream + IDS.txt

# 2.2 prepare the kernel tree (config + build + external-module prep)
KERNELDIR=/path/to/linux-7.3.4 ./scripts/build-kernel.sh
#   - defconfig + virtio/ext4 built-in (direct kernel boot for the VM lab)
#   - make bzImage modules_prepare
#   - copies vmlinux.symvers -> Module.symvers (needed by M= builds)

# 2.3 apply kernel core patches (currently NONE are required)
./scripts/apply-patches.sh /path/to/linux-7.3.4
#   patches/kernel/series is empty by design; if future milestones need
#   exports, each lands as 000N-*.patch documented in docs/*/patches.md.

# 2.4 build the module and userland
./scripts/build-module.sh        # -> module/pf.ko
make -C tools                    # -> tools/out/pfctl (static)

# 2.5 install on a running 7.3.4 system
sudo insmod module/pf.ko         # creates /dev/pf (0600 root)
sudo cp tools/out/pfctl /usr/local/sbin/
sudo pfctl -e                    # enable
sudo pfctl -f /etc/pf.conf       # real OpenBSD 8.0 syntax
```

## 3. Verify

```sh
pfctl -s info            # "Status: Enabled", live counters
pfctl -nf /etc/pf.conf   # parse-only check
dmesg | grep pfk         # hooks registered (netisr wq data path)
```

VM end-to-end suite (Alpine 3.22 on libvirt): `./scripts/vm-create.sh`,
`./scripts/vm-deploy.sh`, `./scripts/vm-run-tests.sh` — see
`docs/en/vm-lab.md`.

## 4. Uninstall / revert

```sh
sudo pfctl -d; sudo rmmod pf          # flushes rulesets/states
./scripts/unpatch.sh /path/to/linux-7.3.4   # reverse-applies any future patches
```

## 5. What "applied" means here

Because the deliverable is an out-of-tree module, "applying the patch set"
= steps 2.1–2.4; the untouched Linux tree is only *configured and built*
(virtio/EXT4 =y) — no source files are modified unless a future milestone
adds a documented `patches/kernel/000N-*.patch` (all such patches: one
section each in docs/en/patches.md with what/why/impact/revert).

## 6. Notes on the steps above (2026-11-12)

- Step **2.3 is a documented no-op today**: the patch set is empty and
  `scripts/apply-patches.sh` / `scripts/unpatch.sh` / `patches/kernel/series`
  are not yet in the tree (they land with the first real core patch — see
  [patches.md](patches.md)). You can safely skip 2.3.
- Step 2.4 builds every userland tool: `make -C tools` targets are `pfctl`,
  `pdivert`, `pflogd` and the ABI harness (`abitest`, `abi`, `test`, `clean`).
- After 2.5, the whole feature set is usable and gated by the lab suite —
  filtering with state, tables and anchors, NAT (`nat-to`/`rdr-to`/`binat-to`
  plus pools), scrub, forced routing (`route-to`/`dup-to`/`reply-to`),
  `divert`, `pflog` and the OpenBSD 8.0 queues (HFSC). The
  [feature matrix](feature-matrix.md) says which gate proves what; read
  [pf-guide.md](pf-guide.md) for the deviations before enabling PF on a path
  you need.

## 6.1 Optional: the narrated screencasts

```sh
export MINIMAX_API_KEY=...          # MiniMax TTS for the narration
./scripts/vm-create.sh               # the lab must be up
./scripts/screencast/build-all.sh    # -> out/screencast/{en,pt}/*.mp4
```

It records real shell sessions against the lab, generates the narration with
a MiniMax female voice (English and Portuguese) and renders 1080p30 MP4s
with burned-in subtitles and the lab's network diagram. Details:
[screencast.md](screencast.md).

## 7. Where to go next

| | |
|---|---|
| Architecture, one page | [guide.md](guide.md) |
| Build stages in detail | [build.md](build.md) |
| VM validation lab | [vm-lab.md](vm-lab.md) |
| Daily rulesets & warnings | [pf-guide.md](pf-guide.md) |
| Known divergences | [caveats.md](caveats.md) |
| Core patch mechanism | [patches.md](patches.md) |
| Narrated screencasts | [screencast.md](screencast.md) |
| Doc index | [README.md](README.md) |
