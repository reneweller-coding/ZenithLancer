"""Machine ears for the spoken story (assets/voice): every take of the chosen texts against the other takes of the
same figure - its speaking rate (seconds per letter), its silence, clipping and its median pitch. A take far from its
figure's usual (a slurred or cut-off take, a voice an octave off, a long silence) is listed for a new take
(tools/make_voices.py --force --only KEY --lang L). Runs on the CPU with the VoiceLab's Python (librosa).

    set PYTHONNOUSERSITE=1
    <VoiceLab>\\env-qwen\\python.exe tools\\check_voices.py [prefix]      (default: e6_)

Writes work/voice_check.txt.
"""
import collections
import os
import re
import sys

import librosa
import numpy as np
import soundfile as sf

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VOICED = ("radio", "briefing", "debrief", "intro", "ending")


def stories(lang):
    """key -> (from, text) of every voiced story entry of a language."""
    out = {}
    folder = os.path.join(ROOT, "data", "story", lang)
    for name in sorted(os.listdir(folder)):
        key = None
        for line in open(os.path.join(folder, name), encoding="utf-8"):
            m = re.match(r"^(\w+) (\S+)", line)
            if m:
                key = m.group(2) if m.group(1) in VOICED else None
                if key:
                    out[key] = ["", ""]
                continue
            if not key:
                continue
            m = re.match(r'^\s+from "(.*)"', line)
            if m:
                out[key][0] = m.group(1)
            elif line.startswith(">"):
                out[key][1] += line[1:].strip() + " "
    return out


def voices():
    """the "from" as written -> the voice (data/lang/speakers.txt)"""
    out = {}
    for line in open(os.path.join(ROOT, "data", "lang", "speakers.txt"), encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        portrait, voice, name = line.rstrip("\n").split(" ", 2)
        out[name] = voice
    return out


def measure(path):
    x, sr = sf.read(path, dtype="float32", always_2d=True)
    mono = x.mean(axis=1)
    dur = len(mono) / sr
    frame = int(sr * 0.05)
    rms = np.sqrt(np.mean(mono[: len(mono) // frame * frame].reshape(-1, frame) ** 2, axis=1) + 1e-12)
    silence = float(np.mean(20 * np.log10(rms) < -45.0))
    peak = float(np.max(np.abs(mono)))
    y = librosa.resample(mono, orig_sr=sr, target_sr=16000)
    f0, _, _ = librosa.pyin(y[: 16000 * 12], fmin=60, fmax=500, sr=16000, frame_length=1024)
    pitch = float(np.nanmedian(f0)) if np.any(np.isfinite(f0)) else 0.0
    return dur, silence, peak, pitch


def main():
    prefix = sys.argv[1] if len(sys.argv) > 1 else "e6_"
    who = voices()
    lines, todo = [], []
    for lang in ("de", "en"):
        texts = stories(lang)
        # Every take measured: the chosen ones, and up to twelve others of each figure as its usual.
        by_voice = collections.defaultdict(list)
        for key, (frm, text) in texts.items():
            path = os.path.join(ROOT, "assets", "voice", lang, key + ".ogg")
            if os.path.exists(path) and len(text) > 20:
                by_voice[who.get(frm, "?")].append((key, text, path))
        for voice, takes in sorted(by_voice.items()):
            mine = [t for t in takes if t[0].startswith(prefix)]
            if not mine:
                continue
            others = [t for t in takes if not t[0].startswith(prefix)][:12]
            base = [measure(p) + (len(t),) for k, t, p in others]
            rate0 = float(np.median([d / n for d, s, pk, pi, n in base])) if base else None
            pitch0 = float(np.median([pi for d, s, pk, pi, n in base if pi > 0])) if base else None
            for key, text, path in mine:
                dur, silence, peak, pitch = measure(path)
                rate = dur / len(text)
                why = []
                if rate0 and not 0.6 < rate / rate0 < 1.6:
                    why.append("pace %.2fx" % (rate / rate0))
                if silence > 0.45:
                    why.append("silence %.0f%%" % (100 * silence))
                if peak > 0.999:
                    why.append("clipped")
                if pitch0 and pitch > 0 and not 0.8 < pitch / pitch0 < 1.25:
                    why.append("pitch %.0f Hz (usual %.0f)" % (pitch, pitch0))
                lines.append("%s %-14s %-10s %5.1fs %4.0f Hz  %s" % (lang, key, voice, dur, pitch, ", ".join(why) or "ok"))
                if why:
                    todo.append((lang, key))
                print(lines[-1], flush=True)
    with open(os.path.join(ROOT, "work", "voice_check.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("%d of %d takes to redo: %s" % (len(todo), len(lines), " ".join("%s:%s" % t for t in todo)))


if __name__ == "__main__":
    main()
