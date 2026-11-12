#!/usr/bin/env python3
"""
Join the phases into one continuous video per language, with transitions.

Each phase fades into a short "chapter" card that names it, and the card
fades into the next phase - one continuous stream, no hard cuts, so the
viewer always knows where they are.

    ./scripts/screencast/join.py --lang en
"""
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(REPO, "out", "screencast")
ART = os.path.join(OUT, "art")

PHASE_ORDER = ["00-ambiente", "01-implementacao", "02-ativacao", "03-regras",
               "04-roteamento", "05-balanceamento", "06-qos"]

TITLES = {
    "00-ambiente": ("The lab environment", "O ambiente de testes",
                    "Four Alpine VMs under libvirt", "Quatro VMs Alpine sob libvirt"),
    "01-implementacao": ("Implementation in the kernel", "Implementacao no kernel",
                         "OpenBSD 8.0 sources + a Linux compatibility layer",
                         "Fontes do OpenBSD 8.0 + camada de compatibilidade Linux"),
    "02-ativacao": ("Activation", "Ativacao",
                    "insmod pf.ko, then pfctl over the OpenBSD ioctl ABI",
                    "insmod pf.ko e o pfctl pela ABI de ioctls do OpenBSD"),
    "03-regras": ("Basic rules", "Regras basicas",
                  "Tables, anchors, keep state, block drop",
                  "Tabelas, anchors, keep state, block drop"),
    "04-roteamento": ("Routing", "Roteamento",
                      "route-to and reply-to", "route-to e reply-to"),
    "05-balanceamento": ("Load balancing", "Balanceamento",
                         "Pools round-robin, source-hash, dup-to",
                         "Pools round-robin, source-hash, dup-to"),
    "06-qos": ("Quality of service", "Qualidade de servico",
               "hfsc queues in the data path", "Filas hfsc no caminho de dados"),
}


def run(cmd):
    subprocess.run(cmd, check=True)


def probe(path):
    out = subprocess.run(["ffprobe", "-v", "error", "-show_entries",
                          "format=duration", "-of", "csv=p=0", path],
                         capture_output=True, text=True).stdout.strip()
    return float(out or 0)


def chapter_card(phase, lang, out_path, seconds=1.8):
    """A short card naming the phase; it is the pivot of the crossfade."""
    title = TITLES[phase][0 if lang == "en" else 1]
    sub = TITLES[phase][2 if lang == "en" else 3]
    num = PHASE_ORDER.index(phase) + 1
    art = None
    if os.path.isdir(ART):
        imgs = sorted(f for f in os.listdir(ART) if f.endswith((".jpg", ".png")))
        if imgs:
            art = os.path.join(ART, imgs[0])

    script = os.path.join(OUT, "_chapter.py")
    with open(script, "w") as f:
        f.write(f"""
import os, sys
from PIL import Image, ImageDraw, ImageFont
W, H = 1920, 1080
BG = (12, 16, 22); ACCENT = (88, 168, 255); FG = (240, 246, 252); MUTED = (140, 150, 165)
mono = "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono.ttf"
sans = "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf"
sansb = "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf"
art = {art!r}
img = Image.new("RGB", (W, H), BG)
if art and os.path.exists(art):
    img = Image.blend(img, Image.open(art).resize((W, H)).convert("RGB"), 0.5)
d = ImageDraw.Draw(img, "RGBA")
d.rectangle([0, 0, W, 8], fill=ACCENT)
d.text((140, 430), {title!r}, font=ImageFont.truetype(sansb, 62), fill=FG)
d.text((140, 520), {sub!r}, font=ImageFont.truetype(sans, 30), fill=MUTED)
d.text((140, 586), "PHASE %d OF %d" % ({num}, {len(PHASE_ORDER)}),
       font=ImageFont.truetype(mono, 24), fill=ACCENT)
d.text((140, 640), "PF on Linux - OpenBSD PF 8.0 no kernel Linux",
       font=ImageFont.truetype(mono, 24), fill=MUTED)
img.save(sys.argv[1])
""")
    png = out_path + ".png"
    run([sys.executable, script, png])
    have = subprocess.run(["ffmpeg", "-hide_banner", "-encoders"],
                          capture_output=True, text=True).stdout
    venc = (["-c:v", "libx264", "-preset", "veryfast", "-crf", "20", "-profile:v", "high"]
            if "libx264" in have else
            ["-c:v", "libopenh264", "-b:v", "6M", "-profile:v", "high"])
    run(["ffmpeg", "-y", "-loglevel", "error", "-loop", "1", "-i", png,
         "-t", str(seconds), "-r", "30"] + venc + ["-pix_fmt", "yuv420p", out_path])
    os.remove(png)
    os.remove(script)
    return out_path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lang", required=True, choices=["en", "pt"])
    ap.add_argument("--out")
    ap.add_argument("--fade", type=float, default=0.7)
    args = ap.parse_args()

    lang = args.lang
    tmp = os.path.join(OUT, "_tmp")
    os.makedirs(tmp, exist_ok=True)

    segs = []          # (path, duration)
    for phase in PHASE_ORDER:
        src = os.path.join(OUT, lang, f"{phase}.mp4")
        if not os.path.exists(src):
            sys.stderr.write(f"[join] missing {src}, skipping\n")
            continue
        segs.append((src, probe(src)))
        card = chapter_card(phase, lang, os.path.join(tmp, f"card-{phase}.mp4"))
        segs.append((card, probe(card)))

    if not segs:
        raise SystemExit("[join] nothing to join")

    inputs = []
    for path, _ in segs:
        inputs += ["-i", path]

    # sequential xfade: each segment fades into the next
    filters = []
    prev = "0:v"
    offset = segs[0][1] - args.fade
    for i in range(1, len(segs)):
        filters.append(f"[{prev}][{i}:v]xfade=transition=fade:"
                       f"duration={args.fade}:offset={offset:.3f}[v{i}]")
        prev = f"v{i}"
        offset += segs[i][1] - args.fade

    # The audio has to follow the same timeline: with xfade the segments
    # overlap by the fade duration, so each one starts `fade` earlier than
    # the plain sum would suggest.
    audio_at = []
    acc = 0.0
    for i, (_, dur) in enumerate(segs):
        audio_at.append(max(0.0, acc - args.fade * i))
        acc += dur

    # Only the phase videos carry narration; the chapter cards are silent.
    # Their input indices are the even ones in `inputs`.
    phase_idx = [i for i in range(len(segs)) if i % 2 == 0]
    afilters = []
    labels = []
    for k, i in enumerate(phase_idx):
        ms = int(audio_at[i] * 1000)
        afilters.append(f"[{i}:a]adelay={ms}|{ms}[a{k}]")
        labels.append(f"[a{k}]")
    afilters.append("".join(labels) +
                    f"amix=inputs={len(phase_idx)}:normalize=0,"
                    f"atrim=0:{audio_at[-1] + segs[-1][1]:.3f},"
                    f"asetpts=N/SR/TB[amix]")
    filters += afilters
    amap = "[amix]"

    out = args.out or os.path.join(OUT, lang, "pf-on-linux-full.mp4")
    have = subprocess.run(["ffmpeg", "-hide_banner", "-encoders"],
                          capture_output=True, text=True).stdout
    if "libx264" in have:
        venc = ["-c:v", "libx264", "-preset", "veryfast", "-crf", "20",
                "-profile:v", "high", "-level", "4.0"]
    elif "libopenh264" in have:
        venc = ["-c:v", "libopenh264", "-b:v", "6M", "-profile:v", "high"]
    else:
        venc = ["-c:v", "libvpx-vp9", "-b:v", "0", "-crf", "30", "-row-mt", "1",
                "-cpu-used", "4"]
    # the phases already carry their narration; the cards are silent
    run(["ffmpeg", "-y", "-loglevel", "error"] + inputs +
        ["-filter_complex", ";".join(filters), "-map", f"[{prev}]",
         "-map", amap] + venc +
        ["-pix_fmt", "yuv420p", "-movflags", "+faststart", out])
    print(f"[join] {out}  {probe(out):.1f}s")


if __name__ == "__main__":
    main()
