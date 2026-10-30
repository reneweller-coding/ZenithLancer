"""Puts chosen takes of a later generation in place of a piece's: each named take levelled like tools/make_music.py
does it and written to assets/music/<cue>.ogg (the first) and <cue>_2.ogg (the second). Rebuild the quiet layers
afterwards (tools/make_layers.py --force --only <cue> <cue>_2).

    python tools/pick_takes.py work/music_reroll/zenith_lancer_reroll e2_b=1,3 [secret=2 ...]

A cue given one take loses its second (<cue>_2.ogg and its layer are deleted): the game then plays the one.
"<cue>_2=N" puts take N in the second place only (the first stays).
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_music import ROOT, level_and_encode   # noqa: E402


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    src = sys.argv[1]
    manifest = json.load(open(os.path.join(src, "manifest.json"), encoding="utf-8"))
    files = {c["id"]: c["levels"][0]["files"] for c in manifest["cues"]}
    out = os.path.join(ROOT, "assets", "music")
    for arg in sys.argv[2:]:
        cue, takes = arg.split("=")
        takes = [int(t) for t in takes.split(",")][:2]
        # "<cue>_2=N": only the second take's place (the first stays as it is).
        second = cue not in files and cue.endswith("_2") and cue[:-2] in files
        if second:
            cue = cue[:-2]
        for n, t in enumerate(takes):
            path = os.path.join(out, "%s%s.ogg" % (cue, "" if n == 0 and not second else "_2"))
            gain = level_and_encode(os.path.join(src, files[cue][t - 1]), cue, path)
            print("%-10s take %d -> %s (%+.1f dB)" % (cue, t, os.path.basename(path), gain))
        if len(takes) == 1 and not second:
            for extra in ("%s_2.ogg" % cue, "%s_2_low.ogg" % cue):
                if os.path.exists(os.path.join(out, extra)):
                    os.remove(os.path.join(out, extra))
                    print("%-10s removed %s" % (cue, extra))


if __name__ == "__main__":
    main()
