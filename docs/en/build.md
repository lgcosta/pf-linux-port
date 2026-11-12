# Build pipeline in detail

Português: [docs/pt-BR/build.md](../pt-BR/build.md).
Quick recipe: [install.md](install.md). This page explains *what each stage
does* and *what has actually been exercised*.

Everything below runs on the build host (the laptop); the VM lab (separate
host "deb") only receives artifacts — see [vm-lab.md](vm-lab.md).

## 0. Prerequisites

From `docs/en/install.md`: gcc ≥ 15, GNU make, bison (flex is not needed —
OpenBSD `parse.y` carries a hand-written `yylex`), `bc`, rsync, `glibc-static`
for the static pfctl, root for `insmod`. Kernel tree expected at
`../linux-7.3.4` (override with `KERNELDIR=`; the root `Makefile` defaults to
`$(HOME)/lab/linux_kernel/linux-7.3.4`).

## 1. Fetch + verify OpenBSD sources — `scripts/fetch-sources.sh`

- Downloads `sys.tar.gz` and `src.tar.gz` from the primary mirror
  `https://openbsd.c3sl.ufpr.br/pub/OpenBSD/8.0/`, falling back to
  `cdn.openbsd.org`.
- Verifies sha256 against the pins in `VERSIONS` (sys: `964b1aed…`, src:
  `9984c74f…`); an already-cached tarball is skipped when its hash matches.
- Landing zone: `_vendor/openbsd/` (gitignored).

## 2. Vendorize — `scripts/vendorize.sh`

Extracts **only the files the port consumes** into `_vendor/upstream/`
(gitignored): the `sys/net/pf*.c|h`, `ifq/hfsc/fq_codel/ip_pflog/ip_divert`,
`radix/rtable/toeplitz`, `crypto/{md5,sha2,siphash}`, `sys/sys/*.h` list and
`sbin/pfctl/*` + headers (see the `SYS_K`/`SRC_K` arrays). It then records
each file's `$OpenBSD: …$` tag into `_vendor/upstream/IDS.txt` — the forensic
baseline for every ported line and for future rebases.

## 3. Staging into the port — `scripts/stage-port.sh` (deterministic porting)

`stage-port.sh` turns pristine upstream files into the files under `module/`:

1. every `#include` is replaced by `pfk.h` + the staged `pfvar` headers
   (kernel branches selected with `-D_KERNEL=1`);
2. the shared rename program (`pf_sed`) resolves BSD↔Linux name collisions
   (`struct ip` → `struct pf_ip`, `ifnet` → `pfk_ifnet`, `ip_output` →
   `pfk_ip_output`, `sa_len` assignments stripped, …);
3. **each run is appended to `docs/porting-notes.md`** — that file is the
   staging log, not prose.

Ported files are then committed and hand-edited in-tree; re-staging is only
for forensics (`stage-port.sh --diff <file>` compares our copy against the
pristine upstream text). Radix/rtable/siphash/toeplitz are deliberately not
auto-staged (hand-ported).

## 4. Kernel — `scripts/build-kernel.sh` (+ `kernel-config.sh`)

- `kernel-config.sh`: starts from `defconfig` if there is no `.config`, then
  sets (idempotently, via `scripts/config` + `make olddefconfig`):
  `VIRTIO_BLK/PCI/NET=y`, `EXT4_FS=y`, `NETFILTER=y`, `NETFILTER_ADVANCED`,
  `NET_SCHED=y`, `INET=y`, `IPV6=y`, `SERIAL_8250(+CONSOLE)=y` — i.e.
  everything needed to direct-boot the pf0 VM with **no initrd**.
  `kernel-config.sh test` additionally enables `DEBUG_INFO` +
  `DEBUG_KMEMLEAK` for soak builds.
- `build-kernel.sh`: `make -j(nproc-2) bzImage modules_prepare`, ensures
  `Module.symvers` (copies `vmlinux.symvers` — kbuild does not write the
  former) and `scripts/module.lds` exist, then drops boot artifacts in
  `out/`: `linux-<ver>-bzImage`, `System.map`, `.config`, `kernelversion`,
  `bzImage.sha256`. Prints `KERNEL_BUILD_OK <kernelrelease>`.
- **Kernel core patches are currently none**: `patches/kernel/` holds no
  `000N-*.patch` (see [patches.md](patches.md)).

## 5. Module — `scripts/build-module.sh`

`make -C "$KERNELDIR" M=module` → `module/pf.ko` (a single module: the BSD
`ifq`/`pfq` interface lives inside it, it is not a separate `tc` qdisc). The
script self-prepares `Module.symvers` / `modules_prepare` first. Object list
(`module/Makefile`): ported engine (`pf.c`, `pf_ioctl.c`, `pf_norm.c`,
`pf_if.c`, `pf_lb.c`, `pf_ruleset.c`, `pf_table.c`, `pf_osfp.c`,
`pf_syncookies.c`, `divert_sock.c`, `pfk_pflog.c`, `hfsc.c`, `fq_codel.c`) +
compat layer (mbuf shim, kern glue, if/ip4/ip6/pcb/xmit/ifq, crypto, radix,
subr_tree) + `pfk_mod.c`/`pfk_hooks.c`. Compile flags define `PFLINUX=1` and
`_KERNEL=1`. Prints `MODULE_BUILD_OK`.

`insmod module/pf.ko` creates `/dev/pf` (0600) and registers the netfilter
hooks; `dmesg` shows `pfk: …` lines (see [caveats.md](caveats.md) for what
the data path does).

## 6. Userland — `make -C tools`

Targets (from `tools/Makefile`): `pfctl`, `pdivert`, `pflogd`, `abitest`,
`abi`, `test`, `clean`.

- `pfctl` — the OpenBSD 8.0 sources compiled against the copied ABI headers;
  result is a **static binary** at `tools/out/pfctl`. Every deviation from
  upstream is listed in `tools/UPSTREAM.md` (7 marked edits; the rest is the
  compat header/source set).
- `abi` — builds `abitest` (userland) and `abitest-module` (same table under
  kernel compat headers), dumps both struct-size/offset tables and `diff`s
  them; a mismatch means the ioctl ABI drifted. `out/abi-*.tbl` are the
  reference inputs.
- `test` — offline parse checks: runs `pfctl -n -f` over `tools/tests/*.conf`
  (basic / translations / queues+scrub / anchor / negative `broken.conf`).
  This is pure userland; it needs no `/dev/pf`.
- `pdivert` — the divert relay, built from `tools/pdivert/`. It talks to the
  `AF_DIVERT` socket family the module registers at runtime
  (`socket(PF_DIVERT=32, SOCK_RAW, IPPROTO_DIVERT=254)`); gate `11-divert.sh`
  proves the round trip.
- `pflogd` — reads `/dev/pflog` and writes a DLT_PFLOG pcap whose record
  layout is byte-compatible with OpenBSD `pflog(4)`; gate `12-pflog.sh`.

## 7. Screencasts — `scripts/screencast/build-all.sh`

Not part of the kernel build, but part of the deliverable. The pipeline:

| Stage | Script | What it does |
|---|---|---|
| narration text | `make_narration.py` | writes `scripts/screencast/narration/<phase>.{en,pt}.json` (one segment per beat of the session) |
| voice | `make_audio.py` | MiniMax TTS, one MP3 per segment, female voice per language |
| session | `record.py` | real SSH session on the lab: commands typed key by key, captured as asciicast v2 |
| video | `render_video.py` | replays the capture through a terminal emulator, draws the lab diagram, burns the subtitles |
| assembly | `assemble.py` | mixes the narration onto the timeline |
| full film | `join.py` | crossfades the phases through chapter cards |

`make screencast` (or the script) runs all of it; it needs
`MINIMAX_API_KEY` and the lab up. Details: [screencast.md](screencast.md).

## 8. Root `Makefile`

`make module | tools | kernel | vm | vm-destroy | test | screencast | docs | clean` — thin
wrappers over the scripts above. `make test` runs the VM suite
([vm-lab.md](vm-lab.md)); `make screencast` renders the videos;
`make docs` just lists the bilingual doc set.

## What has been exercised vs. not

- Stages 1–6 have each been run repeatedly on the build host (artifacts exist
  in-tree: `out/`, `module/pf.ko`, `tools/out/{pfctl,pdivert,pflogd}`), and the
  scripts above were verified line-by-line for this document.
- Stage 7 (the screencasts) has been run end to end for all seven phases in
  both languages; the videos are in `out/screencast/{en,pt}/`.
- A **fresh-clone end-to-end run** (`quickstart-check.sh`, plan task T19) has
  *not* been executed yet; treat the exact command sequence in install.md as
  "as designed, last-run state per milestone" rather than CI-verified.
