"""Makes the trailer (English, about a minute, 1080p) and the README's three GIFs from the game itself: clips of
missions flown by the autopilot (ZenithLancer --video: a hidden window, no sound, no console), title cards in the
game's font, a picture of the workshop, and the title music under all of it. The game draws with the GPU: run it
through the queue like every GPU job.

    python tools/gpu_queue.py python tools/make_trailer.py [--reuse]

    --reuse   keep clips already rendered in work/trailer (only cut again)

Writes dist/trailer/zenith_lancer_trailer.mp4 (upload it with the release; dist/ is not in git) and
docs/shots/motion_*.gif. Needs ffmpeg on the PATH and a built game in bin/msvc (or bin/linux).
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = next((p for p in (os.path.join(ROOT, "bin", "msvc", "ZenithLancer.exe"), os.path.join(ROOT, "bin", "linux", "ZenithLancer"))
            if os.path.exists(p)), None)
WORK = os.path.join(ROOT, "work", "trailer")
OUT = os.path.join(ROOT, "dist", "trailer", "zenith_lancer_trailer.mp4")
FONT_DIR = os.path.join(ROOT, "assets", "fonts")
MUSIC = os.path.join(ROOT, "assets", "music", "title.ogg")
QUIET = (0x08000000 | 0x00004000) if os.name == "nt" else 0   # no console window, below normal priority
FPS = 60

# What the trailer shows, in order: ("card", title, subtitle, seconds) / ("clip", name, game arguments, seconds) /
# ("still", name, game arguments, seconds).
SHOTS = [
    ("card", "ZENITH LANCER", "A vertical shooter with a workshop", 3.0),
    ("clip", "pyra", ["--level", "e1_01", "--from", "18"], 5.0),
    ("clip", "thalassa", ["--level", "e2_05", "--from", "40"], 4.0),
    ("card", "Six episodes across the Solenne system", "", 2.2),
    ("clip", "quarry", ["--level", "e3_10", "--boss"], 5.0),
    ("clip", "sylvara", ["--level", "e4_04", "--from", "30"], 4.0),
    ("clip", "crown", ["--level", "e5_09", "--from", "60"], 4.0),
    ("card", "Build your ship between the missions", "", 2.2),
    ("still", "workshop", ["--screen", "shop"], 3.0),
    ("clip", "curator", ["--level", "e5_10", "--boss"], 6.0),
    ("card", "Arcade for one or two", "", 2.0),
    ("clip", "coop", ["--level", "e2_03", "--arcade", "--pilots", "2", "--from", "30"], 5.0),
    ("clip", "vael", ["--level", "e6_04", "--from", "30"], 4.0),
    ("clip", "echo", ["--level", "e6_10", "--boss"], 6.0),
    ("card", "ZENITH LANCER", "Free and open source  ·  Windows  ·  Linux  ·  Steam Deck", 4.0),
]
GIFS = [("motion_curator", "curator", 3.5), ("motion_vael", "vael", 3.5), ("motion_coop", "coop", 3.5)]


def run(cmd, cwd=None):
    r = subprocess.run(cmd, cwd=cwd, creationflags=QUIET, capture_output=True, text=True, errors="replace")
    if r.returncode:
        sys.exit("failed (%d): %s\n%s" % (r.returncode, " ".join(cmd), (r.stdout + r.stderr)[-800:]))


def esc(t):
    return t.replace("\\", "\\\\").replace(":", "\\:").replace("'", "\\'")


def encode_args():
    return ["-c:v", "libx264", "-preset", "medium", "-crf", "18", "-pix_fmt", "yuv420p", "-r", str(FPS)]


def main():
    if not EXE:
        sys.exit("no built game in bin/")
    reuse = "--reuse" in sys.argv
    os.makedirs(WORK, exist_ok=True)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    parts = []
    for i, shot in enumerate(SHOTS):
        kind, name, extra, secs = shot
        seg = os.path.join(WORK, "seg_%02d.mp4" % i)
        fade = "fade=t=in:st=0:d=0.3,fade=t=out:st=%.2f:d=0.35" % (secs - 0.35)
        if kind == "card":
            title, sub = name, extra
            vf = "drawtext=fontfile=Barlow-Regular.ttf:text='%s':fontcolor=white:fontsize=120:x=(w-text_w)/2:y=(h-text_h)/2-%d" % (
                esc(title), 50 if sub else 0)
            if sub:
                vf += ",drawtext=fontfile=Barlow-Regular.ttf:text='%s':fontcolor=0x8ad0ff:fontsize=46:x=(w-text_w)/2:y=(h/2)+60" % esc(sub)
            run(["ffmpeg", "-y", "-loglevel", "error", "-f", "lavfi", "-i", "color=c=0x05080f:s=1920x1080:d=%.2f:r=%d" % (secs, FPS),
                 "-vf", vf + "," + fade] + encode_args() + [seg], cwd=FONT_DIR)
        elif kind == "still":
            png = os.path.join(WORK, name + ".png")
            if not (reuse and os.path.exists(png)):
                run([EXE, "--size", "1920x1080", "--mute", "--lang", "en"] + extra + ["--shot", png])
            # A slow push in on the picture.
            frames = int(secs * FPS)
            run(["ffmpeg", "-y", "-loglevel", "error", "-loop", "1", "-i", png, "-vf",
                 "scale=3840:-1,zoompan=z='1+0.06*on/%d':x='iw/2-(iw/zoom/2)':y='ih/2-(ih/zoom/2)':d=%d:s=1920x1080:fps=%d,%s"
                 % (frames, frames, FPS, fade), "-t", "%.2f" % secs] + encode_args() + [seg])
        else:
            clip = os.path.join(WORK, name + ".mp4")
            if not (reuse and os.path.exists(clip)):
                run([EXE, "--video", clip, "--seconds", "%.2f" % (secs + 0.5), "--fps", str(FPS), "--lang", "en"] + extra)
            run(["ffmpeg", "-y", "-loglevel", "error", "-i", clip, "-t", "%.2f" % secs, "-an", "-vf", fade] + encode_args() + [seg])
        parts.append(seg)
        print("%-9s %-10s %.1f s" % (kind, name if kind != "card" else "", secs), flush=True)
    # One after the other, the title music under all of it.
    total = sum(s[3] for s in SHOTS)
    listing = os.path.join(WORK, "parts.txt")
    with open(listing, "w", encoding="utf-8") as f:
        for p in parts:
            f.write("file '%s'\n" % p.replace("\\", "/"))
    silent = os.path.join(WORK, "silent.mp4")
    run(["ffmpeg", "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", listing, "-c", "copy", silent])
    run(["ffmpeg", "-y", "-loglevel", "error", "-i", silent, "-stream_loop", "-1", "-i", MUSIC, "-map", "0:v", "-map", "1:a",
         "-af", "afade=t=in:d=0.6,afade=t=out:st=%.2f:d=2.5" % (total - 2.5), "-c:v", "copy", "-c:a", "aac", "-b:a", "192k",
         "-t", "%.2f" % total, OUT])
    print("trailer: %s (%.0f s, %.1f MB)" % (OUT, total, os.path.getsize(OUT) / 1e6), flush=True)
    # The README's GIFs: the field's middle of three clips, small and short.
    for gif, name, secs in GIFS:
        out = os.path.join(ROOT, "docs", "shots", gif + ".gif")
        run(["ffmpeg", "-y", "-loglevel", "error", "-ss", "0.4", "-t", "%.2f" % secs, "-i", os.path.join(WORK, name + ".mp4"), "-vf",
             "crop=ih*0.62:ih:(iw-ih*0.62)/2:0,fps=12,scale=256:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=48:stats_mode=diff[p];"
             "[b][p]paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle", out])
        print("gif: %s (%.1f MB)" % (os.path.basename(out), os.path.getsize(out) / 1e6), flush=True)


if __name__ == "__main__":
    main()
