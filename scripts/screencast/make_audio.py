#!/usr/bin/env python3
"""
Generate the narration audio for every screencast phase with MiniMax TTS.

Female voices, one per language:
    en -> English_ConfidentWoman
    pt -> Portuguese_ConfidentWoman

Each *segment* of a narration script becomes its own audio file, so the
renderer can start the voice exactly when the matching beat appears on
screen and burn the same sentence as a subtitle.

    ./scripts/screencast/make_audio.py --lang en
    ./scripts/screencast/make_audio.py --lang pt --phase 04-roteamento
"""
import argparse
import base64
import json
import os
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
NARR = os.path.join(HERE, "narration")
VO_DIR = os.path.join(HERE, "..", "..", "out", "screencast", "vo")

VOICES = {
    "en": {"voice_id": "English_ConfidentWoman", "language_boost": "English"},
    "pt": {"voice_id": "Portuguese_ConfidentWoman", "language_boost": "Portuguese"},
}

# MiniMax occasionally stumbles on the punctuation of long sentences; keeping
# the text plain (no dashes, no slashes) makes the prosody predictable.
MODEL = "speech-02-hd"
API = "https://api.minimax.io/v1/t2a_v2"


def decode_audio(payload):
    """MiniMax returns the audio as a hex string (it starts with the ASCII
    bytes of the ID3 header, 49443304).  Older/other endpoints use base64,
    so both are handled."""
    if len(payload) > 8 and all(c in "0123456789abcdefABCDEF" for c in payload[:64]):
        return bytes.fromhex(payload)
    data = payload.replace("-", "+").replace("_", "/")
    data += "=" * (-len(data) % 4)
    return base64.b64decode(data, validate=False)


def tts(text, voice, lang, out_path, retries=3):
    body = {
        "model": MODEL,
        "text": text,
        "stream": False,
        "voice_setting": {
            "voice_id": voice["voice_id"],
            "speed": 0.97, "vol": 1.0, "pitch": 0, "emotion": "neutral",
        },
        "audio_setting": {"sample_rate": 44100, "bitrate": 128000,
                          "format": "mp3", "channel": 1},
        "language_boost": voice["language_boost"],
    }
    key = os.environ.get("MINIMAX_API_KEY")
    if not key:
        raise SystemExit("MINIMAX_API_KEY is not set")
    for attempt in range(retries):
        req = urllib.request.Request(
            API, data=json.dumps(body).encode(),
            headers={"Authorization": f"Bearer {key}", "Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=120) as r:
                data = json.loads(r.read())
            if data.get("base_resp", {}).get("status_code") != 0:
                raise RuntimeError(str(data.get("base_resp")))
            audio = data.get("data", {}).get("audio")
            if not audio:
                raise RuntimeError("no audio in response")
            with open(out_path, "wb") as f:
                f.write(decode_audio(audio))
            return True
        except Exception as exc:                     # noqa: BLE001
            sys.stderr.write(f"[tts] attempt {attempt + 1} failed: {exc}\n")
            time.sleep(2 + attempt * 3)
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lang", required=True, choices=["en", "pt"])
    ap.add_argument("--phase", help="only this phase")
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args()

    voice = VOICES[args.lang]
    files = sorted(f for f in os.listdir(NARR) if f.endswith(f".{args.lang}.json"))
    if args.phase:
        files = [f for f in files if f.startswith(args.phase + ".")]

    total = 0
    for fn in files:
        phase = fn.split(".")[0]
        script = json.load(open(os.path.join(NARR, fn)))
        outdir = os.path.abspath(os.path.join(VO_DIR, f"{phase}.{args.lang}"))
        os.makedirs(outdir, exist_ok=True)
        for i, seg in enumerate(script["segments"]):
            out = os.path.join(outdir, f"{i:02d}-{seg['beat']}.mp3")
            if os.path.exists(out) and not args.force:
                print(f"skip {phase}.{args.lang} seg{i} ({seg['beat']})")
                total += 1
                continue
            ok = tts(seg["text"], voice, args.lang, out)
            size = os.path.getsize(out) if os.path.exists(out) else 0
            print(f"{'ok ' if ok else 'FAIL'} {phase}.{args.lang} seg{i} "
                  f"({seg['beat']}) {size} bytes")
            total += 1
    print(f"[audio] {total} segments processed for lang={args.lang}")


if __name__ == "__main__":
    main()
