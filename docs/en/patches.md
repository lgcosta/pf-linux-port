# Linux core patch set

Português: [docs/pt-BR/patches.md](../pt-BR/patches.md).

## Current status: EMPTY

The port requires **zero modifications to the Linux 7.3.4 core**. The design
(spec §5) anticipated one mandatory patch — **P1: export symbols needed by
the PF module** — but the M1 compile-port proved it unnecessary: modpost
reported **no undefined symbols** for `pf.ko`. The compat layer was written
against APIs the stock kernel already exports (`__inet_lookup_established`,
`__inet_lookup_listener`, `__inet6_lookup_established`,
`inet6_lookup_listener`, `__udp4_lib_lookup`, `__udp6_lib_lookup`,
`ip_route_output_flow`/`ip6_route_output_flags` via inline wrappers,
`icmp_send`, …). The evidence and the exact list live in
`patches/kernel/p1-needs.txt`, which also carries a **watch list**: e.g.
`tcp_v4_send_reset` would need an export only if userland RST helpers are
used.

`patches/kernel/` therefore contains no `000N-*.patch` today, and applying
the (empty) set is a no-op.

> Honest note: `scripts/apply-patches.sh`, `scripts/unpatch.sh` and
> `patches/kernel/series` **are in the tree** and are exercised by
> `make test`-adjacent scripts; the series is empty by design, so applying it
> is a verified no-op (`PATCHES_APPLIED=0`). When a real patch is needed, the
> commit that adds it MUST also extend the series file and the rules below.

## The mechanism (how the set is meant to work)

- Every kernel-core change is a standalone file
  `patches/kernel/000N-short-description.patch`, listed in order in
  `patches/kernel/series`, applied to the pristine kernel tree with
  `git apply`-style tooling and reverted the same way
  (`unpatch.sh` reverse-applies).
- The tree must never be left dirty silently: builds that must be
  reproducible apply, build, and record the state (`out/.config`,
  `out/kernelversion`).
- Hard rules for any future patch:
  1. **Additive, zero behavior change** (e.g. `EXPORT_SYMBOL_GPL()` lines
     adjacent to existing export clusters). Anything that alters logic needs
     a design-spec amendment first.
  2. One concern per patch, numbered sequentially — never edit a landed
     patch; supersede with a new number.
  3. A section in this file (both languages) **before** the patch lands:
     what/why/impact/revert + the VM evidence.
  4. The patch must survive `make -C <kerneldir> bzImage modules` from a
     clean tree (CI-of-the-lab: `vm-run-tests.sh` after a fresh boot).

## Template for a future `000N` entry

Copy this block into both `docs/en/patches.md` and `docs/pt-BR/patches.md`
(EN above the pt-BR file's same anchor) when adding `000N-*.patch`:

```
### 000N — <one-line title>

- File: `patches/kernel/000N-<slug>.patch`
- Status: landed (kernel 7.3.4) / superseded by 000M
- Touches: <file(s) in the kernel tree>, <n> added lines, 0 removed
- What: <exact symbols / hooks / Kconfig knobs added>
- Why: <which PF-on-Linux code needs it; why no exported API suffices>
- Impact: <runtime/ABI/memory impact on plain Linux; expected: none>
- Revert: `./scripts/unpatch.sh <kerneldir>` (reverse-apply); module
  then fails to link with the missing symbol named in modpost output.
- Evidence: <VM run id / date / relevant gate output>
```

## Change log of this directory

| Date | Change |
|---|---|
| 2026-11-11 | P1 evaluated after M1 compile-port → **not needed**; `p1-needs.txt` records the evidence and the watch list. Set remains empty. |
