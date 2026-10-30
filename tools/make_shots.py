"""Takes the pictures README.md shows (docs/shots/*.png), in English, 1600 x 900, through the game's own picture mode
(a hidden window, silent): missions at a chosen second flown by the autopilot, screens set up for the picture. A
test folder holds the settings (ZENITH_HOME), the player's own stay untouched.

    python tools/make_shots.py [NAME ...]

Uses the GPU like the game: not beside a CUDA job (tools/gpu_queue.py).
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "msvc", "Release", "ZenithLancer.exe")
OUT = os.path.join(ROOT, "docs", "shots")
HOME = os.path.join(ROOT, "work", "shots_home")

SHOTS = {
    "hero": ["--level", "e1_05", "--at", "45"],
    "workshop": ["--screen", "shop"],
    "plan": ["--screen", "plan"],
    "boss": ["--level", "e1_10", "--at", "160"],
    "thalassa": ["--level", "e2_03", "--at", "40"],
    "shatter": ["--level", "e3_01", "--at", "40"],
    "sylvara": ["--level", "e4_01", "--at", "40"],
    "corona": ["--level", "e5_03", "--at", "40"],
    "coop": ["--level", "e2_05", "--at", "50", "--pilots", "2"],
    "radio": ["--level", "e1_02", "--at", "4"],
    "logbook": ["--screen", "logbook"],
}


def main():
    names = sys.argv[1:] or list(SHOTS)
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(HOME, exist_ok=True)
    with open(os.path.join(HOME, "settings.txt"), "w", encoding="utf-8") as f:
        f.write("language en\nwidth 1600\nheight 900\n")
    env = dict(os.environ, ZENITH_HOME=HOME)
    for n in names:
        out = os.path.join(OUT, n + ".png")
        r = subprocess.run([EXE, "--size", "1600x900", "--mute"] + SHOTS[n] + ["--shot", out], env=env,
                           capture_output=True, text=True, creationflags=0x08000000 if os.name == "nt" else 0)
        print("%-9s %s" % (n, "ok" if r.returncode == 0 and os.path.exists(out) else "FAILED " + r.stdout[-200:]),
              flush=True)


if __name__ == "__main__":
    main()
