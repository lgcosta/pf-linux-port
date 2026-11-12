#!/usr/bin/env python3
"""
Record a *real* interactive shell session on the lab firewall, as asciicast v2.

Why this exists: the first screencast batch rendered a script whose output was
dumped in blocks, which looked like a slideshow.  Here the session is captured
as it happens - an SSH session into the Alpine firewall, commands typed key by
key, the remote tty echoing them, the kernel answering in real time.  The
capture is raw bytes with timestamps, so any renderer can replay it at any
speed, and the same capture serves both narrated languages.

    ./scripts/screencast/record.py --plan plans/04-roteamento.json \\
        --out out/screencast/caps/04-roteamento.cast

The plan is JSON:

    {
      "title": "...", "subtitle": "...",
      "beats": [
        {"id": "b1", "cmds": ["uname -r", "lsmod"]},
        {"id": "b2", "cmds": ["pfctl -f /tmp/x.conf"], "note": "..."}
      ]
    }
"""
import argparse
import errno
import fcntl
import json
import os
import pty
import re
import select
import struct
import subprocess
import sys
import termios
import time

import pyte

# The prompt of the remote shell is "root@pf0:~# ".  A prompt is only accepted
# when it is the *whole* line: while a command is being typed, the line still
# ends with the command text, never with the prompt marker.
PROMPT_RE = re.compile(r"^\S+:[^\r\n]{0,80}[#$]\s*$")
COLS, ROWS = 118, 34


class RemoteShell:
    """An SSH session on a pty, with a terminal emulator on our side."""

    def __init__(self, argv, env=None):
        self.master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))
        e = dict(os.environ)
        e.update({"TERM": "xterm-256color", "LC_ALL": "C.UTF-8", "LANG": "C.UTF-8"})
        if env:
            e.update(env)
        self.proc = subprocess.Popen(argv, stdin=slave, stdout=slave, stderr=slave,
                                     env=e, start_new_session=True, close_fds=True)
        os.close(slave)
        self.screen = pyte.Screen(COLS, ROWS)
        self.screen.set_mode(pyte.modes.LNM)
        self.stream = pyte.ByteStream(self.screen)
        self.events = []       # (t, bytes)

    def pump(self, duration):
        end = time.time() + duration
        while True:
            remaining = end - time.time()
            if remaining <= 0:
                return
            try:
                r, _, _ = select.select([self.master], [], [], min(remaining, 0.05))
            except InterruptedError:
                continue
            if self.master not in r:
                continue
            try:
                data = os.read(self.master, 65536)
            except OSError as exc:
                if exc.errno == errno.EIO:
                    return
                raise
            if not data:
                return
            self.stream.feed(data)
            self.events.append((time.time(), data))

    def last_line(self):
        lines = [l for l in self.screen.display if l.strip()]
        return lines[-1].rstrip() if lines else ""

    def at_prompt(self):
        return bool(PROMPT_RE.match(self.last_line()))

    def wait_prompt(self, timeout=90.0):
        end = time.time() + timeout
        while time.time() < end:
            self.pump(0.05)
            if self.at_prompt():
                self.pump(0.12)
                if self.at_prompt():
                    return True
        return False

    def type_line(self, text, cps=34.0):
        """Type a line the way a person does.

        The remote tty echoes the characters back, exactly like a real
        terminal - that echo is what ends up in the capture, so the video
        shows the command being written, one character at a time.
        """
        for ch in text:
            os.write(self.master, ch.encode())
            time.sleep(1.0 / cps + (ord(ch) % 7) * 0.0035)
        os.write(self.master, b"\r")
        self.pump(0.15)

    def send(self, text):
        os.write(self.master, text.encode())
        self.pump(0.2)


def beat_pacing(phase):
    """Minimum on-screen time per beat, taken from the narration audio.

    Only this phase's narration counts: beat ids repeat across phases, so
    mixing them would stretch a beat by another phase's sentence.
    """
    here = os.path.dirname(os.path.abspath(__file__))
    vo = os.path.abspath(os.path.join(here, "..", "..", "out", "screencast", "vo"))
    out = {}
    for lang in ("en", "pt"):
        script = json.load(open(os.path.join(here, "narration", f"{phase}.{lang}.json")))
        for i, seg in enumerate(script["segments"]):
            audio = os.path.join(vo, f"{phase}.{lang}", f"{i:02d}-{seg['beat']}.mp3")
            if not os.path.exists(audio):
                continue
            dur = subprocess.run(
                ["ffprobe", "-v", "error", "-show_entries", "format=duration",
                 "-of", "csv=p=0", audio],
                capture_output=True, text=True).stdout.strip()
            try:
                d = float(dur)
            except ValueError:
                continue
            out[seg["beat"]] = max(out.get(seg["beat"], 0.0), d + 0.8)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--plan", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--host", default="192.168.122.8")
    ap.add_argument("--jump", default="root@192.168.1.242")
    ap.add_argument("--key", default=os.path.expanduser("~/.ssh/pflab_ed"))
    ap.add_argument("--cps", type=float, default=34.0)
    ap.add_argument("--pace", action="store_true",
                    help="hold each beat long enough for its narration")
    ap.add_argument("--ensure-pf", action="store_true",
                    help="make sure pf.ko is loaded before the capture starts")
    args = ap.parse_args()

    plan = json.load(open(args.plan))
    os.makedirs(os.path.dirname(os.path.abspath(args.out)) or ".", exist_ok=True)

    # A login shell would print the Alpine MOTD; running bash directly with a
    # fixed PS1 gives a clean, deterministic opening - one prompt, no banner.
    # --noediting turns off readline: with it, the line editor redraws the
    # command and the terminal echoes it a second time.  Without it, the
    # command appears exactly once, the way a real terminal shows it.
    argv = ["ssh", "-tt", "-J", args.jump,
            "-o", "StrictHostKeyChecking=accept-new", "-o", "LogLevel=ERROR",
            "-i", args.key, f"root@{args.host}",
            "env PS1='\\u@\\h:\\w\\$ ' LC_ALL=C.UTF-8 LANG=C.UTF-8 "
            "PATH=/opt/pf:$PATH bash --norc --noprofile --noediting -i"]
    sh = RemoteShell(argv)
    sh.pump(2.0)
    sh.wait_prompt(timeout=40)

    if args.ensure_pf:
        # The module has to be in place before the capture starts: a phase
        # that runs `rmmod` (activation) or a lost race with another
        # recording would otherwise leave /dev/pf missing mid-session.
        sh.send("lsmod | grep -q '^pf ' || insmod /opt/pf/pf.ko\n")
        sh.wait_prompt(timeout=60)
        sh.send("pfctl -e\n")
        sh.wait_prompt(timeout=60)

    # Drop the opening prompt: the capture must start with the first command.
    sh.events = []

    # How long each beat has to stay on screen: the narration of that beat
    # must fit, otherwise the voice would talk over the next one.  The
    # longest of the two languages wins, so one capture serves both.
    pace = {}
    if args.pace:
        pace = beat_pacing(plan.get("phase") or os.path.basename(args.plan)[:-5])

    beats = []
    for beat in plan["beats"]:
        b_start = time.time() - sh.events[0][0] if sh.events else 0.0
        for cmd in beat["cmds"]:
            sh.type_line(cmd, cps=args.cps)
            if not sh.wait_prompt(timeout=150):
                sys.stderr.write(f"[record] WARNING: no prompt after: {cmd}\n")
            sh.pump(0.45)
        b_end = time.time() - sh.events[0][0] if sh.events else 0.0
        # hold the screen while the narrator talks about what just happened
        want = pace.get(beat["id"], 0.0)
        left = want - (b_end - b_start)
        if left > 0.1:
            sh.pump(left)
            b_end = time.time() - sh.events[0][0] if sh.events else b_end
        sys.stderr.write(f"[record] {plan.get('phase', '?')} {beat['id']}: "
                         f"{b_start:.1f}-{b_end:.1f}s (narration {want:.1f}s)\n")
        beats.append({"id": beat["id"], "start": b_start, "end": b_end,
                      "note": beat.get("note", "")})

    sh.send("exit\n")
    sh.pump(0.8)

    t0 = sh.events[0][0]
    header = {"version": 2, "width": COLS, "height": ROWS,
              "timestamp": int(time.time()),
              "title": plan.get("title", ""), "subtitle": plan.get("subtitle", "")}
    with open(args.out, "w") as f:
        f.write(json.dumps(header) + "\n")
        for t, data in sh.events:
            f.write(json.dumps([round(t - t0, 4), "o", data.decode("utf-8", "replace")]) + "\n")
    meta = {"title": plan.get("title", ""), "subtitle": plan.get("subtitle", ""),
            "duration": sh.events[-1][0] - t0, "beats": beats}
    with open(args.out.replace(".cast", ".json"), "w") as f:
        json.dump(meta, f, indent=2)
    sys.stderr.write(f"[record] {args.out}: {len(sh.events)} chunks, "
                     f"{meta['duration']:.1f}s, {len(beats)} beats\n")


if __name__ == "__main__":
    main()
