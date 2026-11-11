# PFLINUX — OpenBSD 8.0 pfctl on Linux: upstream provenance and edits

Sources: `_vendor/upstream/sbin/pfctl/` (OpenBSD 8.0-current, see
`_vendor/upstream/IDS.txt`), ABI header: `module/include/net/pfvar.h`
(v1.548), ioctl encoding: `module/include/pfk.h`.
This document lists every deviation from the pristine vendor sources, the
compat layer added under `tools/`, and the warnings emitted by the build.

## Build-time header copies (Makefile rules, never hand-edited)

| tools/include/…            | copied from                              |
|----------------------------|------------------------------------------|
| `net/pfvar.h`              | `module/include/net/pfvar.h` (contract)  |
| `sys/tree.h`, `sys/queue.h`| `_vendor/upstream/sys/sys/{tree,queue}.h`|
| `net/radix.h`              | `_vendor/upstream/sys/net/radix.h`       |
| `net/hfsc.h`, `net/fq_codel.h` | `_vendor/upstream/sys/net/{hfsc,fq_codel}.h` |

`make abi` compiles `abitest.c` once against each header set and diffs the
tables + MD5, proving the copy is in lockstep with `module/include`.

## Compat headers (hand-written, tools/include/)

| file | purpose |
|------|---------|
| `sys/ioctl.h` | glibc `<sys/ioctl.h>` via `#include_next`, then `_IO/_IOW/_IOR/_IOWR` redefined to the PFKXIOC (OpenBSD ioccom.h) encoding copied verbatim from `module/include/pfk.h` (NRBITS 8, TYPEBITS 8, SIZEBITS 13, DIRBITS 2, NRSHIFT 0, TYPESHIFT 8, SIZESHIFT 16, DIRSHIFT 30, NONE 0, WRITE 1, READ 2). This makes every `DIOC*` number in pfvar.h identical to what pf.ko decodes. |
| `pflinux_compat.h` | force-included (`-include`): `<sys/types.h>`, `<strings.h>` (bzero/bcopy), `offsetof`, OpenBSD cdefs fallbacks `__dead`/`__printflike`/`__packed`/`__unused`/`__inline`, `HTONL/NTOL/HTONS/NOSH` (assignment-style, `<sys/param.h>`), `AF_LINK` → `AF_PACKET` (parse.y uses it without `<net/if_dl.h>`), prototypes for `strtonum`, `fgetln`, `freezero`, `uid_from_user`, `gid_from_group`, `pledge`, `unveil`. |
| `sys/sysctl.h` | CTL_*/KERN_MAXCLUSTERS(67)/HW_PHYSMEM64(19) constants (OpenBSD values) + `sysctl()` prototype (glibc dropped `<sys/sysctl.h>`). |
| `sys/syslimits.h` | → `<limits.h>` (PATH_MAX etc. for pfvar.h). |
| `sys/_null.h` | OpenBSD NULL header needed by vendored queue.h/tree.h. |
| `sys/refcnt.h` | userland `struct refcnt { unsigned int rc; }` — same size as the module kernel compat (`refcount_t` = one int); referenced by struct pf_anchor only, never crosses ioctl. |
| `sys/timeout.h` | minimal `struct timeout` — pfvar.h embeds it in `struct pf_pktdelay`, which is kernel-internal (userland state payloads use `struct pfsync_state`). |
| `net/route.h` | userland subset pfvar.h/parse.y need: RTLABEL_LEN 32, RT_TABLEID_MAX 32 (**matches `module/include/pfk.h`**, not OpenBSD's 255), NET_RT_* numbers, `struct rt_tableinfo` (verbatim from obbsd route.h), forward `struct rtentry`. AF_ROUTE/PF_ROUTE come from glibc `<sys/socket.h>` (both are 17 there, same as OpenBSD). |
| `net/if.h` | glibc header via `#include_next` + OpenBSD interface-group ABI (`struct ifg_req`, `struct ifgroupreq`, `SIOCGIFGMEMB` with the OpenBSD number, IFQ_NQUEUES/IFQ_MAXPRIO). Linux has no interface groups: the ioctl fails ENOTTY and group names degrade to "no such group", like an OpenBSD box missing the group. |
| `net/if_dl.h` | `AF_LINK` ≡ `AF_PACKET`; `struct sockaddr_dl` laid out to mirror Linux `sockaddr_ll` so pfctl_parser.c's `sdl_index` cast reads `sll_ifindex` (offset 4). AF_LINK is userland-internal (iftab bookkeeping); it never enters ioctl payloads. |
| `netinet/tcp_fsm.h` | TCPS_* states + TCP_NSTATES 11 (obbsd values); `tcpstates[]` extern (defined in compat). |
| `netinet/ip_icmp.h`, `netinet/icmp6.h`, `netinet/ip.h` | `#include_next` + the ICMP*/ICMP6_*/MLD_*/ND_*/IPTOS_DSCP_CS* constants glibc lacks (verbatim obbsd values). |
| `md5.h` | copy of `_vendor/upstream/sys/crypto/md5.h` minus the kernel-only `__bounded__` annotations. |

## Compat sources (tools/compat-user/)

| file | purpose |
|------|---------|
| `sysctl.c` | `sysctl(3)` emulation for exactly the three MIBs pfctl uses: HW_PHYSMEM64 (sysconf), KERN_MAXCLUSTERS (physmem/2048, the OpenBSD RAM-based sizing), NET_RT_TABLE (`struct rt_tableinfo`, always "exists" for ids ≤ RT_TABLEID_MAX — mirrors module `compat/pfk_rtable.c`: Linux has no per-rtableid FIBs, `rtable_exists()` accepts every id). |
| `strtonum.c` | BSD `strtonum(3)` (glibc has none). |
| `md5.c` | vendor `_vendor/upstream/sys/crypto/md5.c`, **only** the three include lines replaced (`sys/param.h`,`sys/systm.h`,`crypto/md5.h` → `sys/types.h`,`endian.h`,`string.h`,`md5.h`); algorithm byte-identical. |
| `rbtree.c` | OpenBSD's out-of-line generic RB tree (`_rb_insert/_rb_find/_rb_remove/_rb_min/_rb_next/...`) that `sys/tree.h`'s RBT_* macros call; OpenBSD ships it in libc, glibc has none. CLRS-based, verified by a 30k-op/12-seed fuzz harness (order, parent-links, bidirectional walks, find identity). |
| `bsd.c` | `tcpstates[]` (obbsd text), `pledge`/`unveil` no-op stubs (unused by 8.0 sources, kept for future syncs), `fgetln()` over `getline()` with OpenBSD `*len` semantics, `freezero()`, `uid_from_user()`, `gid_from_group()`. |
| `inet_net_pton.c` | BSD `inet_net_pton()` for AF_INET abbreviated CIDR ("10/8", classful defaults) + AF_INET6; glibc 2.43 declares it but exports no symbol. |

`pfctl/pf_ruleset.c`: not copied — the Makefile compiles
`_vendor/upstream/sys/net/pf_ruleset.c` **unmodified** into pfctl (its
`!_KERNEL` branch exists precisely "so we can lend code to pfctl", providing
`pf_anchor_setup`, `pf_find_or_create_ruleset`, `pf_main_anchor`, …).
Verified with `cmp` against vendor on every build (file read-only here).

## Grammar toolchain

Only bison is needed: OpenBSD `parse.y` carries a hand-written `yylex()`
(`flex` unused). bison 3.8 accepts the OpenBSD yacc idioms used here —
prologue `typedef struct {union{...} v; int lineno;} YYSTYPE` plus
`%type <v.member>` dotted tags — with zero pre-processing. Duplicate
`%token RTABLE/RDOMAIN` lines produce warnings (upstream text).

## Edits to the copied pfctl sources (all marked in-file with `/* PFLINUX */`)

1. **parse.y** (declarations section): `expand_label_addr`/`expand_label`
   prototypes: address-family parameter `u_int8_t` → `sa_family_t`.
   Linux `sa_family_t` is `unsigned short` (OpenBSD: `unsigned char`); the
   matching definitions in the same file already use `sa_family_t`.
2. **pfctl_parser.c** (`ifa_load`): dropped two
   `ifa_broadaddr->sa_len != 0` / `ifa_dstaddr->sa_len != 0` guards —
   glibc `struct sockaddr` has no `sa_len`; `getifaddrs(3)` only fills those
   members when a broadcast/destination address actually exists.
3. **pfctl_parser.h**: `geticmptypebynumber`, `geticmptypebyname`,
   `geticmpcodebynumber`, `geticmpcodebyname` prototype af parameters
   `u_int8_t` → `sa_family_t` (same reason as #1).
4. **pfctl_parser.c**: `print_fromto` forward prototype, af parameter
   `u_int8_t` → `sa_family_t`.
5. **pf_print_state.c** (`print_name`): removed `sin_len`/`sin6_len`
   assignments; added local `socklen_t slen` set to `sizeof(*sin|*sin6)` and
   passed to `getnameinfo(3)` (no `ss_len` on Linux).
6. **pfctl_table.c** (`print_ad` reverse-DNS): identical `sa_len` removal
   pattern as #5.
7. **pfctl_osfp.c** (`pfctl_file_fingerprints`): a missing `/etc/pf.os`
   (ENOENT) returns success instead of warn+fail — OpenBSD ships the file in
   base; this port has no fingerprint packaging yet. Other open errors still
   fatal. (Without this, every `-f` load exits 1 on stock Linux.)

No other lines differ from vendor; `cmp` each file against
`_vendor/upstream/sbin/pfctl/` to audit (`diff -u` shows only the sites above).

## ABI table notes (tools/abitest/abitest.c)

* The contract's `pf_state_key` is kernel-only in 8.0 (moved to
  `<net/pfvar_priv.h>`, which userland MUST NOT include). The table
  therefore probes its userland mirror `struct pf_state_key_cmp`
  ("keep synced with struct pf_state_key, used in RB_FIND") plus the
  wire-format `struct pfsync_state_key` / `struct pfsync_state` used by
  `DIOCGETSTATE` payloads. The kernel-side comparison of the real
  `struct pf_state_key` is the integrator's job, same as for all
  `-D_KERNEL` layout checks.

## Known warnings (intentional, benign)

* `-Wformat=` (×51): `%lld`/`%llu` vs `long` printf conversions. OpenBSD
  prints 64-bit values with `%ll*`; LP64 glibc types differ in spelling only.
  Output text is identical.
* `-Waddress-of-packed-member` (×6): `struct pfsync_state` is `__packed`
  (wire format shared with pf.ko); pf_print_state.c takes addresses of its
  members. Access is alignment-safe on x86-64 (no AVX requirement).
* `-Wother` (×6): bison "symbol RTABLE/RDOMAIN redeclared" — upstream
  parse.y declares these tokens twice.
* `-Wpointer-sign` (×15) / `-Wformat-truncation` (×10) / `-Wdiscarded-qualifiers`
  (×2): snprintf/`char*` signedness and const-preserving idioms from OpenBSD
  sources (e.g. `strrchr` result into non-const, GCC's path-insensitive
  truncation heuristics). Non-fatal, semantics unchanged.

## Out of scope / pending

* `pdivert` and `pflogd` — intentionally NOT ported here; owned by the
  integrator. (No skeleton stubs are shipped.)
* Kernel-side ABI confirmation (same table compiled under `-D_KERNEL` with
  the module compat headers) — integrator step; `out/abi-tools.tbl` /
  `out/abi-module.tbl` + `out/abitest` md5 line are the reference inputs.
* Runtime paths (`-s`, `-F`, live `-f` load) require `/dev/pf` from pf.ko and
  were exercised only through the `-n` (offline) code path here.
