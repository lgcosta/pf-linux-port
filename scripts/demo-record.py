#!/usr/bin/env python3
"""
PF-on-Linux demo recorder -> asciicast v2 (.cast)

Every byte shown comes from the REAL lab (Alpine VMs under libvirt, kernel
7.3.4 built from source with our pf.ko).  Only the pacing is scripted.

Usage:
    ./scripts/demo-record.py [--out out/demo-pf-linux.cast] [--quick]

Play back with:   asciinema play out/demo-pf-linux.cast
Upload with:      asciinema upload out/demo-pf-linux.cast
"""
import argparse
import json
import os
import shlex
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEB = os.environ.get("DEB", "root@192.168.1.242")
PF0 = os.environ.get("PF0_IP", "192.168.122.8")
CLI = os.environ.get("CLI_IP", "10.0.1.10")
SRV = os.environ.get("SRV_IP", "10.0.2.20")
KEY = os.path.join(os.environ.get("HOME", "/root"), ".ssh/pflab_ed")

CONF = """# PF-on-Linux demo ruleset (pf.conf syntax, real OpenBSD pfctl)
set block-policy return
table <trusted> { 10.0.1.0/24 }

# transit traffic: stateful, table-driven
pass in quick on ingress proto icmp from <trusted> keep state
pass out quick on egress proto icmp keep state

# deny a specific destination
block drop in quick proto icmp to 10.0.9.99

# everything else
pass in quick
pass out quick
"""

DIVERT_CONF = """pass in quick proto icmp from 10.0.1.10 to 10.0.1.1 divert-to "10.0.1.1" port 10000 keep state
pass out quick
"""


def sh(cmd, timeout=40):
    p = subprocess.run(cmd, shell=True, capture_output=True, text=True,
                       timeout=timeout)
    return (p.stdout or "") + (p.stderr or "")


def on_pf0(cmd, timeout=40):
    return sh(f"ssh -o ConnectTimeout=8 -o StrictHostKeyChecking=accept-new "
              f"-i {KEY} root@{PF0} {shlex.quote(cmd)}", timeout)


def on_vm(ip, cmd, timeout=40):
    return sh(f"ssh -o ConnectTimeout=8 -o StrictHostKeyChecking=accept-new "
              f"-i {KEY} root@{ip} {shlex.quote(cmd)}", timeout)


def on_deb(cmd, timeout=40):
    return sh(f"ssh -o ConnectTimeout=8 {DEB} {shlex.quote(cmd)}", timeout)


def stage():
    """put the demo ruleset + pdivert on pf0 (through the lab host)"""
    with open("/tmp/demo-pf.conf", "w") as f:
        f.write(CONF)
    with open("/tmp/demo-divert.conf", "w") as f:
        f.write(DIVERT_CONF)
    sh(f"scp -q /tmp/demo-pf.conf /tmp/demo-divert.conf "
       f"{os.path.join(REPO, 'tools/out/pdivert')} {DEB}:/tmp/", timeout=90)
    on_deb(f"scp -q -o StrictHostKeyChecking=accept-new -i {KEY} "
           f"/tmp/demo-pf.conf /tmp/demo-divert.conf /tmp/pdivert "
           f"root@{PF0}:/tmp/ 2>&1", timeout=90)


SCENARIO = [
    ("local", "echo '=== PF on Linux — OpenBSD PF 8.0 engine running inside the Linux 7.3.4 kernel ==='"),
    ("local", "echo 'lab: 3 Alpine VMs under libvirt (pf0 = firewall, cli/srv = transit endpoints)'"),
    ("pf0", "uname -r"),
    ("pf0", "lsmod | grep '^pf ' || echo 'pf: not loaded'"),
    ("pf0", "insmod /opt/pf/pf.ko && dmesg | tail -3"),
    ("pf0", "/opt/pf/pfctl -s info | head -4"),
    ("pf0", "/opt/pf/pfctl -e && /opt/pf/pfctl -s info | head -1"),
    ("pf0", "cat /tmp/demo-pf.conf"),
    ("pf0", "/opt/pf/pfctl -f /tmp/demo-pf.conf && echo 'rules loaded' && /opt/pf/pfctl -sr"),
    ("pf0", "/opt/pf/pfctl -sr -vv | head -8"),
    ("cli", f"ping -c2 -W2 {SRV} | tail -3"),
    ("pf0", "/opt/pf/pfctl -ss"),
    ("pf0", "/opt/pf/pfctl -t trusted -T show"),
    ("pf0", "/opt/pf/pfctl -s Tables"),
    ("cli", "ping -c2 -W2 10.0.9.99 | tail -3"),
    ("pf0", "/opt/pf/pfctl -sr -vv | head -6"),
    ("pf0", "/opt/pf/pfctl -f /tmp/demo-divert.conf && echo 'divert rule loaded'"),
    ("pf0", "(setsid /tmp/pdivert -p 10000 -f >/tmp/pdivert.log 2>&1 </dev/null &); sleep 1; pgrep pdivert"),
    ("cli", "ping -c2 -W2 10.0.1.1 | tail -3"),
    ("pf0", "head -3 /tmp/pdivert.log"),
    ("pf0", "pkill pdivert; /opt/pf/pfctl -f /dev/null; /opt/pf/pfctl -d; echo 'pf disabled'"),
]


def record(out_path, quick=False):
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    events = []
    t = 0.0

    def emit(text, delay):
        nonlocal t
        t += delay
        events.append([round(t, 3), "o", text])

    header = {
        "version": 2, "width": 100, "height": 26,
        "timestamp": int(time.time()),
        "title": "PF on Linux — OpenBSD PF engine inside the Linux kernel (lab demo)",
        "env": {"SHELL": "/bin/sh", "TERM": "xterm-256color"},
    }
    runners = {
        "pf0": lambda c: on_pf0(c),
        "cli": lambda c: on_vm(CLI, c),
        "srv": lambda c: on_vm(SRV, c),
        "local": lambda c: sh(c),
    }
    labels = {"pf0": "pftest-pf0", "cli": "pftest-cli", "srv": "pftest-srv",
              "local": "lab-host"}

    stage()
    with open(out_path, "w") as f:
        f.write(json.dumps(header) + "\n")
        for kind, cmd in SCENARIO:
            emit(f"\x1b[1;36m{labels[kind]}\x1b[0m:\x1b[1;37m {cmd}\x1b[0m\r\n",
                 0.35)
            try:
                out = runners[kind](cmd)
            except subprocess.TimeoutExpired:
                out = "[timeout]\r\n"
            except Exception as e:  # noqa: BLE001
                out = f"[error: {e}]\r\n"
            out = out.replace("\r\n", "\n").replace("\n", "\r\n")
            emit(out or "(no output)\r\n", min(1.4, 0.3 + len(out) / 5000.0))
        with open(out_path, "a") as f:
            for ev in events:
                f.write(json.dumps(ev) + "\n")
    print(f"WROTE {out_path}: {len(events)} events, {t:.1f}s")
    print("play:   asciinema play " + out_path)
    print("upload: asciinema upload " + out_path)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(REPO, "out",
                                                  "demo-pf-linux.cast"))
    ap.add_argument("--quick", action="store_true")
    a = ap.parse_args()
    record(a.out, a.quick)
