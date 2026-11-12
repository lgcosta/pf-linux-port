# Using PF-on-Linux day to day

Português: [docs/pt-BR/pf-guide.md](../pt-BR/pf-guide.md).

This is the "what can I actually run today" guide. It follows the
[feature matrix](feature-matrix.md): **M2 scope (filtering core: pass/block/
quick/keep state/tables/anchors + enable/disable)** has VM evidence behind it;
everything beyond that is either COMPILES-untested or PENDING. Install/build
is covered by [install.md](install.md) and [build.md](build.md).

## Daily loop

```sh
sudo insmod module/pf.ko          # creates /dev/pf
sudo pfctl -nf /etc/pf.conf       # parse check (userland only — always safe)
sudo pfctl -f /etc/pf.conf        # load ruleset
sudo pfctl -e                     # ENABLE — traffic starts hitting the ruleset NOW
sudo pfctl -s info                # status + counters
sudo pfctl -s states              # states listing (see caveat below)
sudo pfctl -t blk -T add 10.0.1.10   # live table update
sudo pfctl -a admin -f /etc/pf.d/admin.conf
sudo pfctl -d                     # disable
sudo rmmod pf                     # unload (rule/state flush happens here)
```

> **Warning — order matters**: `-f` then `-e`. Enabling with no matching `pass` rule
> for your admin path blocks it — this port follows OpenBSD's default-block
> policy, and the M2 gate runs have *actually cut the test VM's ssh* that
> way (the lab keeps a serial console for exactly this). On a remote host,
> pass your management interface before you enable anything.

## A ruleset that works in this port today

```pf
# /etc/pf.conf — M2 scope only. Interface names here match the VM lab
# (mgmt/lan/wan); substitute your own (eth0, enp1s0, …).

# 1) keep yourself reachable (do this first, always)
pass in  quick on mgmt
pass out quick

# 2) filter core — proven today: quick, proto/port matching, state, drop
table <blk> persist                       # live blocklist (gate 03 pattern)
block in quick proto tcp from <blk> to any

pass in  quick on lan proto tcp from 10.0.1.0/24 to any port { 80 443 } keep state
block drop in quick on wan proto icmp from any to any

# 3) anchors with precedence (gate 04 pattern)
anchor "admin"
```

Macros (`xif = "lan"`), nested `anchor "in/*"`, `label`, `tag`, `set
timeout/limit/state-policy/block-policy/skip` and the rest of the OpenBSD 8.0
grammar **parse** fine — `make -C tools test` exercises them offline — but
the *kernel behavior* of most `set` options has not been gate-verified yet;
treat them as load-and-pray until the matrix moves them.

## What to avoid until the milestones land

| Syntax | Until | What happens today |
|---|---|---|
| `nat-to`, `rdr-to` (`binat-to` pending) | — | **Verified in the lab** (gates `05-nat`, `06-rdr`, the latter pulls 2 MB through a redirect and compares it byte for byte). `nat-to (egress)` expands the OpenBSD interface *group*; on a multi-NIC host prefer the explicit `nat-to (<iface>)` form. |
| `scrub (…)`, `set reassemble` | M3 | `pf_norm.c` runs inside `pf_test` (compiled) — crafted-fragment behavior is untested; treat results as unvalidated. |
| `divert-to`, `divert` tag | — | **Verified in the lab** (gate `11-divert`): the packet is delivered to an `AF_DIVERT` socket (`PF_DIVERT=32`) served by `tools/pdivert`; with no listener it is dropped, exactly like BSD's `divs_noport`. |
| `route-to`, `dup-to`, `fail-to`, `reply-to` | M4 | Not implemented as Linux routing glue yet; rules may load but the behavior is undefined — avoid. |
| `queue` / `queue on` / `pfctl -s queue` | M5 | Parse and load. Queuing falls back to the priq-shaped compat `ifq` layer; **no bandwidth enforcement happens until the `pfq` qdisc lands**. |
| `log`, `log all`, `block log`, `pflogd` | — | **Verified in the lab** (gate `12-pflog`): run `pflogd /tmp/x.pcap` (reads `/dev/pflog`); the file is a `DLT_PFLOG` (117) pcap readable by tcpdump/wireshark. |
| `route-label …` | never (non-goal) | Rejected at the ioctl layer. |
| per-container (netns) policy | out of scope | PF is one global instance in `init_net`; network namespaces see **no** PF. |

## Reading state correctly

`pfctl -s states` shows the real table, including translations, in OpenBSD's
own format: a NAT/redirect state prints both tuples, e.g.

```
all icmp 10.0.2.1:59335 (10.0.1.10:4380) -> 10.0.2.20:8   0:0
all tcp  10.0.2.20:80 (10.0.1.1:8080) <- 10.0.1.10:42091  FIN_WAIT_2:FIN_WAIT_2
```

The value in parentheses is the *original* address before translation.  A
state that exists but shows `0:0` counters simply means the engine has not
seen traffic on it since the last poll; use `pfctl -s info` counters and
`tcpdump` for traffic evidence, as the gates do.

## Kernel log signals you should know

```
pfk: PF-on-Linux engine loaded (OpenBSD 8.0 base)     ← insmod ok
pfk: netfilter hooks registered (10, netisr wq data path)
pfk: divert port N: no socket bound      # pdivert/pflogd consumer not listening
```

Unloading stops the purge timers and drains the work queue first (an rmmod
hang observed in the lab was fixed that way).  The soak gate (`14-soak.sh`)
runs traffic, then `rmmod` → `insmod` → `rmmod` and compares the slab
footprint, so unload/reload is verified for admin use; hot-reload automation
under sustained load is still not a supported workflow.

Full list of behavioral divergences: [caveats.md](caveats.md).
