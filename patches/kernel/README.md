# Kernel core patches — PF-on-Linux

**Current state: the patch set is EMPTY by design.**

The port is an out-of-tree module (`module/pf.ko`). The only kernel-tree
prerequisite is a *configured build* of Linux 7.3.4 (`scripts/build-kernel.sh`:
defconfig + virtio/EXT4 built-in + `modules_prepare`), which touches **no
source file**.

## Verification that nothing is exported-missing

`modpost` reports **zero undefined symbols** for pf.ko against stock 7.3.4
(M1, 2026-11-11; tracked in docs/porting-notes.md).  When future milestones
(M4 divert, M5 pfq qdisc, M6 hardening) hit a non-exported helper, the
requirement is recorded here as `p1-needs.txt` entries and materialized as:

```
patches/kernel/0001-<slug>.patch
patches/kernel/0002-<slug>.patch
...
patches/kernel/series          # ordered list consumed by apply-patches.sh
```

## Rules every patch must satisfy

1. Additive only: `EXPORT_SYMBOL*()` lines or Kconfig/Makefile hooks — never
   behavior changes to existing code paths.
2. One logical change per patch; subject lines prefixed `net: export ...` /
   `pf_port: ...`.
3. Each patch gets a matching section in `docs/en/patches.md` and
   `docs/pt-BR/patches.md`: what / why / where / impact / revert.
4. `scripts/apply-patches.sh <kerneldir>` and `scripts/unpatch.sh <kerneldir>`
   apply/remove the whole series idempotently.
