# PF-on-Linux documentation (English)

Documentação em português: [`docs/pt-BR/README.md`](../pt-BR/README.md).

This is the user documentation set for the port of the **OpenBSD 8.0 PF
packet filter** to **Linux 7.3.4** — the real PF engine (`pf.c`, `pf_norm.c`,
`pf_table.c`, `pf_lb.c`, `hfsc.c`, …) running as the out-of-tree module
`pf.ko`, driven by the real OpenBSD `pfctl` over the `/dev/pf` ioctl ABI.

## Index

| Doc | What it covers |
|---|---|
| [guide.md](guide.md) | One-page architecture: components, data path, control plane, milestones |
| [install.md](install.md) | Apply/build/install recipe (kernel, module, tools) |
| [debian13.md](debian13.md) | Full from-zero implementation on Debian 13 (trixie): host prep, kernel/module/tools build, libvirt lab, stress test |
| [build.md](build.md) | Build pipeline in detail: fetch → vendorize → kernel → module → pfctl |
| [vm-lab.md](vm-lab.md) | The libvirt validation lab: topology, recreate/run, what each of the 17 gates proves |
| [feature-matrix.md](feature-matrix.md) | Per-feature status: VERIFIED-in-VM / VERIFIED-offline / COMPILES-untested / NOT-ENABLED |
| [pf-guide.md](pf-guide.md) | Daily use: what works today in `/etc/pf.conf`, and the deviations to know about |
| [screencast.md](screencast.md) | The narrated videos (English and Portuguese): phases, pipeline, how to re-record |
| [patches.md](patches.md) | Linux core patch set (currently empty) + rules and template for future `000N-*.patch` |
| [caveats.md](caveats.md) | Known divergences from OpenBSD behavior and current limitations |

## Sources of truth

The docs describe the code as it exists in this tree. When in doubt, the
authoritative references are:

- `docs/superpowers/specs/2026-11-11-pf-linux-port-design.md` — design
- `docs/superpowers/plans/2026-11-11-pf-linux-port.md` — milestone plan (tasks T1–T19)
- `docs/porting-notes.md` — staging log + the deviation record (the truth about compat-layer divergences)

Feature-status vocabulary used across these docs:

- **VERIFIED-in-VM** — exercised end-to-end in the libvirt lab; the gate that proves it is named (see [vm-lab.md](vm-lab.md)).
- **VERIFIED-offline** — proven by a fixture or ABI test, not by traffic.
- **COMPILES-untested** — the code is built into `pf.ko`/`pfctl` but no gate has exercised it.
- **NOT-ENABLED** — the code exists but the feature is off in this build.
- **OUT-OF-SCOPE** — non-goal by design (spec §3).

The lab suite is **17/17 green** (`./scripts/vm-run-tests.sh`).
