"""Measures the synthesised effects (ZenithLancer --dumpsfx work/sfx): peak and short-term loudness of each, so the
music's loudness (tools/make_music.py) can be set against them.

    python tools/measure_sfx.py work/sfx
"""
import os
import sys

import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_music import lufs, peak_db  # noqa: E402


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else "work/sfx"
    rows = []
    for name in sorted(os.listdir(folder)):
        if not name.endswith(".wav"):
            continue
        x, fs = sf.read(os.path.join(folder, name), dtype="float32", always_2d=True)
        # Short effects: the loudness of their loudest 400 ms block counts, not the gated average.
        rows.append((name[:-4], peak_db(x), lufs(x, fs), len(x) / fs))
    for n, p, l, d in rows:
        print("%-10s peak %6.1f dB   loudness %6.1f LUFS   %.2f s" % (n, p, l, d))


if __name__ == "__main__":
    main()
