#!/usr/bin/env python3
"""
PF-on-Linux demo recorder — produces an asciicast v2 (.cast) from a REAL
session driven against the lab VMs (no simulated output: every byte shown
comes from the live systems; only pacing is scripted).

Usage:  ./scripts/demo-record.py [--out out/demo-pf-linux.cast]
Requires: lab running (pftest-pf0/cli/srv), repo built, ssh alias `pf0`
(ProxyJump) and lib.sh-style nested access working.

Edit DEMO below to change the narration script. Timing: each line is a
(real command, real output) pair; delays are cosmetic only.
"""
import argparse, json, os, pty, re, shlex, subprocess, sys, time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DEFAULT = os.path.join(REPO, "out", "demo-pf-linux.cast")

DEB = os.environ.get("DEB", "root@192.168.1.242")
PF0_IP = os.environ.get("PF0_IP", "192.168.122.8")


def deb_run(cmd: str, timeout=30) -> str:
    """run a command on the libvirt host (which has direct keys to VMs)"""
    p = subprocess.run(["ssh", "-o", "ConnectTimeout=10", DEB, cmd],
                       capture_output=True, text=True, timeout=timeout)
    return (p.stdout or "") + (p.stderr or "")


def vm0(cmd: str, timeout=30) -> str:
    return deb_run(
        f"ssh -o StrictHostKeyChecking=accept-new -o ConnectTimeout=8 "
        f"-i /root/.ssh/pflab_ed root@{PF0_IP} {shlex.quote(cmd)}",
        timeout)


def vm_ip(ip: str, cmd: str, timeout=30) -> str:
    return deb_run(
        f"ssh -o StrictHostKeyChecking=accept-new -o ConnectTimeout=8 "
        f"-i /root/.ssh/pflab_ed root@{ip} {shlex.quote(cmd)}",
        timeout)


CLI, SRV = "10.0.1.10", "10.0.2.20"


def scene(title):
    return ("#banner", title)

# (shell-line, runner, command)  — runners: pf0 / cli / srv / local
DEMO = [
    ("local",  "echo 'PF-on-Linux — OpenBSD PF engine on Linux 7.3.4 (lab: Alpine VMs)'"),
    ("local",  "uname -r | sed 's/^/build host kernel: /'"),
    ("pf0",    "uname -r"),
    ("pf0",    "lsmod | grep -E '^pf ' || echo 'pf: not loaded yet'"),
    ("pf0",    "insmod /opt/pf/pf.ko && dmesg | tail -2"),
    ("pf0",    "/opt/pf/pfctl -s info | head -3"),
    ("pf0",    "/opt/pf/pfctl -e && /opt/pf/pfctl -s info | head -1"),
    ("pf0",    "cat /tmp/pfdemo.conf"),
    ("pf0",    "/opt/pf/pfctl -f /tmp/pfdemo.conf && /opt/pf/pfctl -sr"),
    ("cli",    f"ping -c 2 -W 2 {SRV} 2>&1 | tail -2"),
    ("pf0",    "/opt/pf/pfctl -s states"),
    ("pf0",    "/opt/pf/pfctl -s Tables"),
    ("pf0",    "/opt/pf/pfctl -t demo -T show"),
    ("cli",    "ping -c 2 -W 2 10.0.9.99 2>&1 | tail -2"),
    ("pf0",    "/opt/pf/pfctl -s info | grep -E 'match|current'"),
    ("pf0",    "/opt/pf/pfctl -ss | head -2"),
]

CONF = """set block-policy return
set loginterface egress
table <demo> { 10.0.1.0/24 }
match in quick on ingress proto icmp from <demo> tag DEMO
pass in quick on ingress proto icmp keep state
pass out quick proto icmp keep state
block drop in quick proto icmp from any to 10.0.9.99
"""


def prep():
    # stage the demo ruleset into pf0
    import base64
    b64 = base64.b64encode(CONF.encode()).decode()
    vm0(f"echo {b64} | base64 -d > /tmp/pfdemo.conf; echo staged")


def record(out_path: str, fps_pad=0.4):
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    casts = []
    t = 0.0

    def emit(text, delay):
        nonlocal t
        t += delay
        casts.append([round(t, 3), "o", text])

    header = {"version": 2, "width": 100, "height": 24,
              "timestamp": int(time.time()), "title":
              "PF on Linux — port demo (OpenBSD PF 8.0 engine on Linux 7.3.4, Alpine libvirt lab)",
              "env": {"SHELL": "/bin/sh", "TERM": "xterm-256color"}}
    with open(out_path, "w") as f:
        f.write(json.dumps(header) + "\n")

        def show_prompt(host):
            emit(f"\x1b[1;36m{host}$\x1b[0m ", 0.15)

        runners = {"pf0": vm0, "cli": lambda c: vm_ip(CLI, c),
                   "srv": lambda c: vm_ip(SRV, c), "local":
                   lambda c: subprocess.run(["sh", "-c", c],
                                            capture_output=True, text=True,
                                            timeout=30).stdout +
                   subprocess.run(["sh", "-c", c], capture_output=True,
                                  text=True).stderr}
        prep()
        for kind, cmd in DEMO:
            label = {"pf0": "pftest-pf0", "cli": "pftest-cli",
                     "srv": "pftest-srv", "local": "build-host"}[kind]
            show_prompt(label)
            emit(cmd + "\r\n", 0.25)
            try:
                out = runners[kind](cmd)
            except Exception as e:
                out = f"[runner error: {e}]\n"
            # strip ANSI-safe passthrough; small human-ish delay
            delay = min(1.2, 0.25 + len(out) / 4000.0)
            emit(out.replace("\n", "\r\n") or "(no output)\r\n", delay)
            time.sleep(0.1)
        emit("\x1b[1;36mpftest-pf0$\x1b[0m /opt/pf/pfctl -d\r\n", 0.4)
        runners["pf0"]("/opt/pf/pfctl -d")
        for i, ev in enumerate(casts):
            f.write(json.dumps(ev) + "\n")
    print(f"WROTE {out_path} ({len(casts)} events, {t:.1f}s)")
    print("play:  asciinema play " + out_path +
          "   (or: script-replay/pf-in-play fallback: cat with 'less -R')")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=OUT_DEFAULT)
    a = ap.parse_args()
    record(a.out)
