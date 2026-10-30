"""Makes the quiet layer of every stage piece, assets/music/<piece>_low.ogg beside <piece>.ogg (and <piece>_2.ogg).
The game plays both in step and fades between them by how much happens on the field (Audio::intensity): the quiet
layer while the field is calm, the full piece when it fills.

The quiet layer is the piece itself through a zero-phase low pass (the loop wrapped round, so the seam stays clean):
the two are the same music, sample for sample in phase, and the fade opens a filter instead of laying two different
arrangements over each other. --cover makes ACE-Step covers instead (the first try, 30.10.2026: they kept the energy,
CLAP heard nothing calmer, and two arrangements at once blur).

    <AudioGenerator>\\venv\\Scripts\\python.exe tools\\make_layers.py [--force] [--only e1_a ...]
        [--cover]   (on the GPU, tools/gpu_queue.py)

Needs ffmpeg with libvorbis on the PATH.
"""
import argparse
import glob
import json
import os
import re
import sys
import tempfile
import time

import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_music  # noqa: E402  (lufs, peak_db, encode)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
import paths   # noqa: E402  (tools/paths.py: where the AudioGenerator lives)
STAGE = re.compile(r"^(e[1-6]_[ab]|secret)(_2)?$")
LOW_STYLE = ("stripped back sparse arrangement of the same piece, the melody kept on soft synths, warm pads and a "
             "pulsing bass, light hi-hats instead of the full drum kit, no heavy drums, calmer and quieter, "
             "instrumental video game background music")
STRENGTH = 0.55      # covers: how close to the source (structure and melody kept, the arrangement thinned)
LOW_DB = -3.0        # covers: the quiet layer under the full piece's loudness
CUTOFF = 700.0       # the low pass: the bass and the body stay, the leads and the cymbals go


def filtered(x, fs):
    """The piece through a zero-phase low pass, the loop wrapped round so its start and end meet as before."""
    from scipy.signal import butter, sosfiltfilt
    pad = min(len(x) // 2, int(2.0 * fs))
    wrapped = np.concatenate([x[-pad:], x, x[:pad]])
    y = sosfiltfilt(butter(3, CUTOFF, btype="low", fs=fs, output="sos"), wrapped, axis=0)
    return y[pad:pad + len(x)].astype(np.float32)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--force", action="store_true", help="make the layers again")
    ap.add_argument("--only", nargs="*", help="only these pieces (e1_a, secret_2 ...)")
    ap.add_argument("--cover", action="store_true", help="ACE-Step covers instead of the low pass (GPU)")
    args = ap.parse_args()
    spec = json.load(open(os.path.join(ROOT, "tools", "music.json"), encoding="utf-8"))
    cues = {c["id"]: c for c in spec["cues"]}
    music = os.path.join(ROOT, "assets", "music")
    todo = []
    for path in sorted(glob.glob(os.path.join(music, "*.ogg"))):
        name = os.path.basename(path)[:-4]
        m = STAGE.match(name)
        if not m or (args.only and name not in args.only):
            continue
        out = os.path.join(music, name + "_low.ogg")
        if os.path.isfile(out) and not args.force and os.path.getmtime(out) >= os.path.getmtime(path):
            continue
        todo.append((name, m.group(1), path, out))
    if not todo:
        print("every layer is there")
        return
    if not args.cover:
        for name, cue, path, out in todo:
            x, fs = sf.read(path, dtype="float32", always_2d=True)
            # No gain: the same level in the low band, so the fade between the two keeps the bass steady.
            make_music.encode(filtered(x, fs), fs, out, 4)
            print("%-10s low pass" % name, flush=True)
        print("layers: %d made" % len(todo))
        return

    sys.path.insert(0, paths.audiogen())
    here = os.getcwd()
    os.chdir(paths.audiogen())
    from core.backends import backend_class
    from core.config import acestep_env, load_config
    from core.request import SongRequest

    cfg = load_config()
    os.environ.update(acestep_env(cfg))
    section = dict(cfg.get("acestep") or {})
    backend = backend_class("acestep")()
    t0 = time.time()
    backend.load(section.get("variant"), section.get("planner") or "none", section, lambda s: None)
    print("model loaded in %.0f s" % (time.time() - t0), flush=True)
    work = tempfile.mkdtemp(prefix="zl_layers_")
    for n, (name, cue, path, out) in enumerate(todo):
        c = cues.get(cue, {})
        x, fs = sf.read(path, dtype="float32", always_2d=True)
        src = os.path.join(work, name + ".flac")
        sf.write(src, x, fs)
        req = SongRequest(task="cover", title="Zenith Lancer - %s (low)" % name,
                          style=", ".join(s for s in (c.get("style", ""), LOW_STYLE) if s),
                          lyrics="[Instrumental]", instrumental=True, bpm=int(c.get("bpm") or spec["bpm"]),
                          key=c.get("key") or spec["key"], timesig="4", seed=int(spec["seed"]) + 101 * (n + 1), count=1,
                          audio_format="flac", source_audio=src, strength=STRENGTH)
        job = os.path.join(work, name)
        os.makedirs(job, exist_ok=True)
        t = time.time()
        takes, _ = backend.generate(req, __import__("pathlib").Path(job), lambda v, s: None, lambda s: None)
        y, fy = sf.read(str(takes[0].path), dtype="float32", always_2d=True)
        if fy != fs:
            # Resampled to the source's rate so both streams run in step sample for sample.
            t_src = np.arange(int(len(y) * fs / fy)) / fs
            y = np.stack([np.interp(t_src, np.arange(len(y)) / fy, y[:, ch]) for ch in range(y.shape[1])], axis=1)
        if y.shape[1] != x.shape[1]:
            y = np.repeat(y[:, :1], x.shape[1], axis=1)
        # Exactly the source's length: the game loops both together.
        y = y[:len(x)] if len(y) >= len(x) else np.concatenate([y, np.zeros((len(x) - len(y), y.shape[1]), np.float32)])
        if float(np.max(np.abs(y[-1] - y[0]))) > 0.01:
            k = int(0.003 * fs)
            ramp = (0.5 - 0.5 * np.cos(np.linspace(0.0, np.pi, k)))[:, None].astype(np.float32)
            y[:k] *= ramp
            y[-k:] *= ramp[::-1]
        gain = min(make_music.lufs(x, fs) + LOW_DB - make_music.lufs(y, fs), make_music.PEAK_DB - make_music.peak_db(y))
        make_music.encode(y * 10.0 ** (gain / 20.0), fs, out, 4)
        print("%-10s %+5.1f dB  %.0f s" % (name, gain, time.time() - t), flush=True)
    os.chdir(here)
    print("layers: %d made" % len(todo))


if __name__ == "__main__":
    main()
