#!/usr/bin/env python3
"""
Record a *real* interactive shell session and render it as a video.

The difference to the earlier renderer: the shell is not a script whose output
is dumped at once.  We open a real pty, type the commands **key by key** with
human-like timing, wait for the prompt to come back, and capture the byte
stream that the terminal actually produced.  The result is a session that
looks like somebody sitting in front of a terminal - characters appearing one
by one, output scrolling in as the kernel answers.

    ./scripts/screencast/session.py --plan scripts/screencast/plans/04.sh \\
        --out out/screencast/session-04.mp4 --title "..." --subtitle "..."

The plan file is a shell script executed *inside* the recording pty.  Every
line the plan prints is real output from the lab.
"""
import argparse
import errno
import fcntl
import os
import pty
import select
import shlex
import struct
import subprocess
import sys
import termios
import time

import pyte
from PIL import Image, ImageDraw, ImageFont

# ---------------------------------------------------------------- palette ---
BG = (11, 14, 19)
PANEL = (18, 22, 28)
BORDER = (44, 50, 58)
FG = (203, 211, 219)
HEADER_BG = (21, 26, 33)
HEADER_FG = (240, 246, 252)
ACCENT = (86, 166, 255)
ACCENT2 = (57, 197, 207)
MUTED = (108, 116, 128)
GREEN = (63, 185, 80)
YELLOW = (210, 153, 34)
RED = (255, 123, 114)
TITLE_BG = (15, 19, 25)

NAMED = {
    "black": (48, 54, 61), "red": RED, "green": GREEN, "yellow": YELLOW,
    "blue": ACCENT, "magenta": (188, 140, 255), "cyan": ACCENT2,
    "white": FG, "brown": (168, 115, 66), "default": None,
}


def color_of(spec, default):
    if spec == "default":
        return default
    if spec in NAMED:
        return NAMED[spec] or default
    try:
        if len(spec) == 6:
            return tuple(int(spec[i:i + 2], 16) for i in (0, 2, 4))
    except ValueError:
        pass
    return default


# ------------------------------------------------------------------ fonts ---
def load_fonts():
    mono = "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono.ttf"
    monob = "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono-Bold.ttf"
    return (ImageFont.truetype(mono, 20), ImageFont.truetype(monob, 20),
            ImageFont.truetype(mono, 18))


# ---------------------------------------------------------------- session ---
class Shell:
    """A real interactive shell on a pty; we type into it and read what it says."""

    PROMPT = "__PS1__"

    def __init__(self, argv, cols=118, rows=34, env=None):
        self.cols, self.rows = cols, rows
        self.master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ,
                    struct.pack("HHHH", rows, cols, 0, 0))
        e = dict(os.environ)
        e.update({"TERM": "xterm-256color", "COLUMNS": str(cols),
                  "LINES": str(rows), "LC_ALL": "C.UTF-8", "PS1": self.PROMPT + " "})
        if env:
            e.update(env)
        self.proc = subprocess.Popen(argv, stdin=slave, stdout=slave, stderr=slave,
                                     env=e, start_new_session=True, close_fds=True)
        os.close(slave)
        self.screen = pyte.Screen(cols, rows)
        self.stream = pyte.ByteStream(self.screen)
        self.events = []          # (t, snapshot)
        self.t0 = time.time()
        self.raw = bytearray()
        self.idle = 0.0

    def pump(self, duration):
        """Read from the pty for `duration` seconds, recording screen states."""
        end = time.time() + duration
        while True:
            remaining = end - time.time()
            if remaining <= 0:
                return
            try:
                r, _, _ = select.select([self.master], [], [], min(remaining, 0.05))
            except InterruptedError:
                continue
            if self.master in r:
                try:
                    data = os.read(self.master, 65536)
                except OSError as exc:
                    if exc.errno == errno.EIO:
                        return
                    raise
                if not data:
                    return
                self.raw += data
                self.stream.feed(data)
                self.events.append((time.time() - self.t0, self.snapshot()))
                self.idle = 0.0
            else:
                self.idle += 0.05
                if self.idle > 0.25:
                    # nothing new: keep the timeline going so the frame clock
                    # stays uniform
                    self.events.append((time.time() - self.t0, self.snapshot()))

    def snapshot(self):
        rows = []
        for y in range(self.rows):
            line = self.screen.buffer[y]
            cells = []
            for x in range(self.cols):
                ch = line[x]
                cells.append((ch.data or " ", color_of(ch.fg, FG), ch.bold))
            rows.append(cells)
        return rows, (self.screen.cursor.y, self.screen.cursor.x)

    def wait_prompt(self, timeout=25.0):
        """Wait until the shell prompt shows up again."""
        end = time.time() + timeout
        while time.time() < end:
            self.pump(0.05)
            text = "\n".join(self.screen.display)
            if text.rstrip().endswith(self.PROMPT):
                return True
        return False

    def type_line(self, text, cps=42.0):
        """Type a line character by character, like a human."""
        for ch in text:
            self.write(ch)
            # slight jitter so it does not look machine-perfect
            delay = 1.0 / cps + (hash(ch) % 7) * 0.0016
            time.sleep(delay)
        self.write("\r")
        self.pump(0.12)

    def write(self, s):
        os.write(self.master, s.encode())

    def run(self):
        self.pump(1.2)
        return self.proc.wait()


# --------------------------------------------------------------- rendering ---
def render(rows, cursor, title, subtitle, phase_no, phase_total, font, bold,
           small, footer, panel_label, art=None, art_alpha=0.0):
    W, H = 1920, 1080
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)

    d.rectangle([0, 0, W, 70], fill=HEADER_BG)
    d.text((44, 24), "PF on Linux", font=bold, fill=HEADER_FG)
    tw = d.textlength("PF on Linux", font=bold)
    d.text((44 + tw + 24, 28), "-", font=font, fill=MUTED)
    d.text((44 + tw + 44, 28), "OpenBSD PF 8.0 engine inside the Linux kernel",
           font=font, fill=MUTED)
    if phase_total:
        label = f"FASE {phase_no}/{phase_total}"
        lw = d.textlength(label, font=small)
        d.text((W - 44 - lw, 30), label, font=small, fill=ACCENT)

    d.text((44, 100), title, font=bold, fill=HEADER_FG)
    if subtitle:
        d.text((44, 148), subtitle, font=font, fill=MUTED)

    top, left, right = 196, 44, W - 44
    lh = 28
    d.rectangle([left, top, right, H - 78], fill=PANEL, outline=BORDER, width=2)
    d.rectangle([left, top, right, top + 40], fill=HEADER_BG)
    for i, c in enumerate([(255, 95, 86), (255, 189, 46), (39, 201, 63)]):
        d.ellipse([left + 22 + i * 24, top + 14, left + 34 + i * 24, top + 26], fill=c)
    d.text((left + 100, top + 12), panel_label, font=small, fill=MUTED)

    x0, y0 = left + 26, top + 58
    cw = d.textlength("M", font=font)
    for i, line in enumerate(rows):
        y = y0 + i * lh
        if y > H - 96:
            break
        for x, (ch, fg, b) in enumerate(line):
            if ch == " ":
                continue
            d.text((x0 + x * cw, y), ch, font=bold if b else font, fill=fg)

    if cursor:
        cy, cx = cursor
        if 0 <= cy < len(rows) and 0 <= cx < len(rows[0]):
            d.rectangle([x0 + cx * cw, y0 + cy * lh, x0 + (cx + 1) * cw - 1,
                         y0 + (cy + 1) * lh - 1], fill=ACCENT)

    d.text((44, H - 52), footer, font=small, fill=MUTED)
    url = "git.labexposed.com/lgcosta/pf-linux-port"
    uw = d.textlength(url, font=small)
    d.text((W - 44 - uw, H - 52), url, font=small, fill=MUTED)
    return img


def render_title(title, subtitle, phase_no, phase_total, bold, font, small, art):
    W, H = 1920, 1080
    img = Image.new("RGB", (W, H), TITLE_BG)
    if art:
        a = art.resize((W, H)).convert("RGB")
        img = Image.blend(img, a, 0.55)
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, W, 8], fill=ACCENT)
    d.text((120, 420), title, font=bold, fill=(250, 252, 255))
    if subtitle:
        d.text((120, 490), subtitle, font=font, fill=(190, 200, 214))
    if phase_total:
        d.text((120, 570), f"FASE {phase_no} DE {phase_total}", font=small, fill=ACCENT)
    d.text((120, 630), "PF on Linux - OpenBSD PF 8.0 no kernel Linux",
           font=small, fill=(150, 160, 175))
    return img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--plan", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--title", required=True)
    ap.add_argument("--subtitle", default="")
    ap.add_argument("--footer", default="PF on Linux - OpenBSD PF 8.0 no kernel Linux - lab libvirt")
    ap.add_argument("--panel", default="alpine (pf0) - lab libvirt - kernel 7.3.4 + pf.ko")
    ap.add_argument("--phase-no", type=int, default=0)
    ap.add_argument("--phase-total", type=int, default=0)
    ap.add_argument("--cols", type=int, default=118)
    ap.add_argument("--rows", type=int, default=34)
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--title-card", type=float, default=2.5)
    ap.add_argument("--art")
    args = ap.parse_args()

    font, bold, small = load_fonts()
    art = None
    if args.art and os.path.exists(args.art):
        art = Image.open(args.art)

    # The plan runs inside the recording shell; the shell itself is a real
    # bash with our prompt, so what the user sees is a live session.
    sh = Shell(["bash", "--norc", "--noprofile", "-i"], cols=args.cols, rows=args.rows)
    sh.pump(1.0)
    # make the prompt look like the OpenBSD box
    sh.write("export PS1='\\u@\\h:\\w\\$ ' PS2='> ' TERM=xterm-256color\n")
    sh.pump(0.6)
    sh.write("export LC_ALL=C.UTF-8 LANG=C.UTF-8; clear\n")
    sh.pump(0.8)
    sh.events = [(0.0, sh.snapshot())]

    # run the plan: each line is typed into the shell
    plan = open(args.plan).read()
    for raw_line in plan.split("\n"):
        line = raw_line.rstrip()
        if not line.strip() or line.strip().startswith("#"):
            continue
        if line.startswith("@sleep "):
            sh.pump(float(line.split()[1]))
            continue
        if line.startswith("@run "):
            # run a command without showing it typed (used for setup)
            sh.write(line[5:] + "\n")
            sh.wait_prompt(timeout=30)
            continue
        sh.type_line(line)
        sh.wait_prompt(timeout=60)
        sh.pump(0.45)

    sh.write("exit\n")
    sh.pump(0.5)

    sys.stderr.write(f"[session] {len(sh.events)} events, {sh.events[-1][0]:.1f}s\n")

    # ------------------------------------------------------ frame timeline ---
    frames_dir = os.path.join(os.path.dirname(os.path.abspath(args.out)), "_sframes")
    subprocess.run(["rm", "-rf", frames_dir], check=False)
    os.makedirs(frames_dir, exist_ok=True)

    n = 0
    title_img = render_title(args.title, args.subtitle, args.phase_no, args.phase_total,
                             bold, font, small, art)
    for _ in range(int(args.title_card * args.fps)):
        title_img.save(os.path.join(frames_dir, f"{n:05d}.png"))
        n += 1

    # the events carry real timestamps: map them onto a uniform frame clock
    ev = sh.events
    t0 = ev[0][0]
    for i, (ts, snap) in enumerate(ev):
        hold = (ev[i + 1][0] - ts) if i + 1 < len(ev) else 0.04
        hold = max(0.033, min(0.5, hold))
        rows, cur = snap
        img = render(rows, cur, args.title, args.subtitle, args.phase_no,
                     args.phase_total, font, bold, small, args.footer, args.panel, art)
        cnt = max(1, int(round(hold * args.fps)))
        for _ in range(cnt):
            img.save(os.path.join(frames_dir, f"{n:05d}.png"))
            n += 1

    subprocess.run(["rm", "-rf", frames_dir], check=False)


if __name__ == "__main__":
    main()
