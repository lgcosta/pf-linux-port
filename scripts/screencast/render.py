#!/usr/bin/env python3
"""
Render a shell session into a silent, YouTube-ready MP4 screencast.

Pipeline:  pty -> raw bytes -> pyte (terminal emulation) -> PNG frames -> MP4

Every byte on screen is the real output of the command; only the pacing and
the framing are produced here.  No audio track is written (the videos are
silent by design).

Usage:
    ./scripts/screencast/render.py --script scripts/screencast/phases/02-ativacao.sh \
        --out out/screencast/02-ativacao.mp4 --title "Fase 2 - Ativacao" \
        --subtitle "insmod pf.ko / pfctl -e / pfctl -s info"
"""
import argparse
import errno
import fcntl
import os
import pty
import select
import shutil
import signal
import struct
import subprocess
import sys
import termios
import time

import pyte
from PIL import Image, ImageDraw, ImageFont

# ---------------------------------------------------------------- palette ---
BG = (14, 17, 23)
PANEL = (20, 24, 31)
BORDER = (48, 54, 61)
FG = (201, 209, 217)
HEADER_BG = (24, 29, 38)
HEADER_FG = (240, 246, 252)
ACCENT = (88, 166, 255)
MUTED = (110, 118, 129)
TITLE_BG = (17, 21, 28)

# pyte colour names -> RGB (a calm, high-contrast terminal palette)
NAMED = {
    "black": (48, 54, 61),
    "red": (255, 123, 114),
    "green": (63, 185, 80),
    "yellow": (210, 153, 34),
    "blue": (88, 166, 255),
    "magenta": (188, 140, 255),
    "cyan": (57, 197, 207),
    "white": (201, 209, 217),
    "brown": (168, 115, 66),
    "default": None,
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


class Recorder:
    """Runs a command on a pty and keeps a timestamped snapshot per output event."""

    def __init__(self, cols, rows, env=None):
        self.cols, self.rows = cols, rows
        self.screen = pyte.Screen(cols, rows)
        self.screen.set_mode(pyte.modes.LNM)
        self.stream = pyte.ByteStream(self.screen)
        self.events = []          # (timestamp, rows_of_cells)
        self.t0 = time.time()

    def snapshot(self):
        rows = []
        for y in range(self.rows):
            line = self.screen.buffer[y]
            cells = []
            for x in range(self.cols):
                ch = line[x]
                fg = color_of(ch.fg, FG)
                bold = ch.bold
                cells.append((ch.data or " ", fg, bold))
            rows.append(cells)
        return rows, (self.screen.cursor.y, self.screen.cursor.x)

    def run(self, argv, env=None):
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ,
                    struct.pack("HHHH", self.rows, self.cols, 0, 0))
        e = dict(os.environ)
        e.update({"TERM": "xterm-256color", "COLUMNS": str(self.cols),
                  "LINES": str(self.rows), "LC_ALL": "C.UTF-8"})
        if env:
            e.update(env)
        proc = subprocess.Popen(argv, stdin=slave, stdout=slave, stderr=slave,
                                env=e, start_new_session=True, close_fds=True)
        os.close(slave)
        while True:
            try:
                r, _, _ = select.select([master], [], [], 0.25)
            except InterruptedError:
                continue
            if master in r:
                try:
                    data = os.read(master, 65536)
                except OSError as exc:
                    if exc.errno == errno.EIO:
                        break
                    raise
                if not data:
                    break
                self.stream.feed(data)
                rows, cur = self.snapshot()
                self.events.append((time.time() - self.t0, rows, cur))
            elif proc.poll() is not None:
                # drain whatever is left
                try:
                    while True:
                        data = os.read(master, 65536)
                        if not data:
                            break
                        self.stream.feed(data)
                        rows, cur = self.snapshot()
                        self.events.append((time.time() - self.t0, rows, cur))
                except OSError:
                    pass
                break
        proc.wait()
        os.close(master)
        if not self.events:
            rows, cur = self.snapshot()
            self.events.append((time.time() - self.t0, rows, cur))
        return proc.returncode


# ------------------------------------------------------------- rendering ---
def load_font(path, size):
    return ImageFont.truetype(path, size)


def render_frame(rows, title, subtitle, phase_no, phase_total, cols, font, bold_font,
                small_font, footer, cursor=None, panel="alpine (pf0)"):
    W, H = 1920, 1080
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)

    # header
    d.rectangle([0, 0, W, 74], fill=HEADER_BG)
    d.text((44, 26), "PF on Linux", font=bold_font, fill=HEADER_FG)
    tw = d.textlength("PF on Linux", font=bold_font)
    d.text((44 + tw + 26, 30), "-", font=font, fill=MUTED)
    d.text((44 + tw + 46, 30), "OpenBSD PF 8.0 engine inside the Linux kernel",
           font=font, fill=MUTED)
    if phase_total:
        label = f"FASE {phase_no}/{phase_total}"
        lw = d.textlength(label, font=small_font)
        d.text((W - 44 - lw, 32), label, font=small_font, fill=ACCENT)

    # title block
    d.text((44, 112), title, font=bold_font, fill=HEADER_FG)
    if subtitle:
        d.text((44, 162), subtitle, font=font, fill=MUTED)

    # terminal panel, sized to the content
    top = 212
    left = 44
    right = W - 44
    lh = 30
    used = 0
    for i, line in enumerate(rows):
        if any(c[0].strip() for c in line):
            used = i + 1
    bottom = min(H - 84, top + 44 + 28 + (max(used, 4) + 1) * lh)
    d.rectangle([left, top, right, bottom], fill=PANEL, outline=BORDER, width=2)
    d.rectangle([left, top, right, top + 44], fill=HEADER_BG)
    for i, c in enumerate([(255, 95, 86), (255, 189, 46), (39, 201, 63)]):
        d.ellipse([left + 24 + i * 26, top + 15, left + 38 + i * 26, top + 29], fill=c)
    d.text((left + 110, top + 13), panel, font=small_font, fill=MUTED)

    # text
    x0 = left + 28
    y0 = top + 64
    cw = d.textlength("M", font=font)
    for i, line in enumerate(rows):
        y = y0 + i * lh
        if y > bottom - 10:
            break
        for x, (ch, fg, bold) in enumerate(line):
            if ch == " ":
                continue
            d.text((x0 + x * cw, y), ch, font=bold_font if bold else font, fill=fg)

    # cursor block
    if cursor:
        cy, cx = cursor
        if 0 <= cy < len(rows) and 0 <= cx < cols:
            d.rectangle([x0 + cx * cw, y0 + cy * lh, x0 + (cx + 1) * cw - 1,
                         y0 + (cy + 1) * lh - 1], fill=ACCENT)

    # footer
    d.text((44, H - 58), footer, font=small_font, fill=MUTED)
    url = "git.labexposed.com/lgcosta/pf-linux-port"
    uw = d.textlength(url, font=small_font)
    d.text((W - 44 - uw, H - 58), url, font=small_font, fill=MUTED)
    return img


def render_title(title, subtitle, phase_no, phase_total, bold_font, font, small_font):
    W, H = 1920, 1080
    img = Image.new("RGB", (W, H), TITLE_BG)
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, W, 8], fill=ACCENT)
    d.text((120, 400), title, font=bold_font, fill=HEADER_FG)
    if subtitle:
        d.text((120, 470), subtitle, font=font, fill=MUTED)
    if phase_total:
        d.text((120, 560), f"FASE {phase_no} DE {phase_total}", font=small_font, fill=ACCENT)
    d.text((120, 620), "PF on Linux - OpenBSD PF 8.0 no kernel Linux",
           font=small_font, fill=MUTED)
    return img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--script", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--title", required=True)
    ap.add_argument("--subtitle", default="")
    ap.add_argument("--footer", default="")
    ap.add_argument("--phase-no", type=int, default=0)
    ap.add_argument("--phase-total", type=int, default=0)
    ap.add_argument("--cols", type=int, default=120)
    ap.add_argument("--rows", type=int, default=32)
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--max-hold", type=float, default=2.0)
    ap.add_argument("--min-hold", type=float, default=0.25)
    ap.add_argument("--title-card", type=float, default=3.0)
    ap.add_argument("--keep-frames", action="store_true")
    ap.add_argument("--panel", default="alpine (pf0) - lab libvirt - kernel 7.3.4 + pf.ko")
    args = ap.parse_args()

    repo = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    mono = "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono.ttf"
    mono_b = "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono-Bold.ttf"
    font = load_font(mono, 21)
    bold_font = load_font(mono_b, 21)
    small_font = load_font(mono, 19)

    rec = Recorder(args.cols, args.rows)
    rc = rec.run(["bash", args.script])
    sys.stderr.write(f"[screencast] script rc={rc}, events={len(rec.events)}\n")

    os.makedirs(os.path.dirname(os.path.abspath(args.out)) or ".", exist_ok=True)
    frames_dir = os.path.join(os.path.dirname(os.path.abspath(args.out)), "_frames")
    shutil.rmtree(frames_dir, ignore_errors=True)
    os.makedirs(frames_dir, exist_ok=True)

    def dump(img, idx):
        img.save(os.path.join(frames_dir, f"{idx:05d}.png"))

    n = 0
    # title card
    title_img = render_title(args.title, args.subtitle, args.phase_no,
                             args.phase_total, bold_font, font, small_font)
    title_frames = int(args.title_card * args.fps)
    for _ in range(title_frames):
        dump(title_img, n)
        n += 1

    # session frames, paced by the real inter-event delays
    prev_t = 0.0
    for i, (ts, rows, cur) in enumerate(rec.events):
        hold = ts - prev_t
        prev_t = ts
        if i + 1 < len(rec.events):
            hold = rec.events[i + 1][0] - ts
        hold = max(args.min_hold, min(args.max_hold, hold))
        img = render_frame(rows, args.title, args.subtitle, args.phase_no,
                           args.phase_total, args.cols, font, bold_font,
                           small_font, args.footer, cursor=cur, panel=args.panel)
        frames = max(1, int(round(hold * args.fps)))
        for _ in range(frames):
            dump(img, n)
            n += 1

    # closing card
    end_img = render_frame(rec.events[-1][1], args.title, args.subtitle,
                           args.phase_no, args.phase_total, args.cols, font,
                           bold_font, small_font,
                           args.footer or "fim da fase",
                           cursor=rec.events[-1][2], panel=args.panel)
    for _ in range(int(1.5 * args.fps)):
        dump(end_img, n)
        n += 1

    # encoder: prefer x264, fall back to openh264 (both H.264 in MP4, which is
    # what YouTube wants), then VP9/WebM.
    have = subprocess.run(["ffmpeg", "-hide_banner", "-encoders"],
                          capture_output=True, text=True).stdout
    base = ["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(args.fps),
            "-i", os.path.join(frames_dir, "%05d.png"),
            "-pix_fmt", "yuv420p", "-movflags", "+faststart", "-r", str(args.fps)]
    if "libx264" in have:
        cmd = base + ["-c:v", "libx264", "-preset", "medium", "-crf", "20",
                      "-profile:v", "high", "-level", "4.0", args.out]
    elif "libopenh264" in have:
        cmd = base + ["-c:v", "libopenh264", "-b:v", "6M",
                      "-profile:v", "high", args.out]
    elif "libvpx-vp9" in have:
        cmd = base + ["-c:v", "libvpx-vp9", "-b:v", "0", "-crf", "30",
                      "-row-mt", "1", "-cpu-used", "4", args.out]
    else:
        raise SystemExit("[screencast] no usable H.264/VP9 encoder in ffmpeg")
    subprocess.run(cmd, check=True)
    if not args.keep_frames:
        shutil.rmtree(frames_dir, ignore_errors=True)
    dur = subprocess.run(["ffprobe", "-v", "error", "-show_entries",
                          "format=duration", "-of", "csv=p=0", args.out],
                         capture_output=True, text=True).stdout.strip()
    size = os.path.getsize(args.out)
    sys.stderr.write(f"[screencast] wrote {args.out} frames={n} "
                     f"duration={dur}s size={size/1e6:.1f}MB\n")


if __name__ == "__main__":
    main()
