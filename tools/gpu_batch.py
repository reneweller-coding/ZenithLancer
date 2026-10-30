"""Makes every GPU asset one step after the other, each step through tools/gpu_queue.py (it waits for a free GPU and
holds the flag only while it runs, so other sessions get the GPU between steps): the music (both specs), the music's
quiet layers, the portraits, the voices. A failed step is told and the next one runs. Logs: work/gpu_<step>.log.

    python tools/gpu_batch.py [step ...]      (music layers portraits voices; default all)
"""
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AUDIOGEN = r"G:\Tools\PythonSkripts\AudioGenerator\venv\Scripts\python.exe"
VOICELAB = r"G:\Tools\VoiceLab\env-qwen\python.exe"
STEPS = {
    "music": [AUDIOGEN, "tools/make_music.py"],
    "layers": [AUDIOGEN, "tools/make_layers.py"],
    "portraits": [AUDIOGEN, "tools/make_portraits.py"],
    "voices": [VOICELAB, "tools/make_voices.py"],
}


def main():
    steps = sys.argv[1:] or list(STEPS)
    env = dict(os.environ, PYTHONNOUSERSITE="1", PYTHONIOENCODING="utf-8", PYTHONUTF8="1")
    for s in steps:
        t = time.time()
        print("%s  %s: waiting for the GPU" % (time.strftime("%H:%M"), s), flush=True)
        with open(os.path.join(ROOT, "work", "gpu_%s.log" % s), "w", encoding="utf-8") as log:
            r = subprocess.run([sys.executable, "tools/gpu_queue.py"] + STEPS[s], cwd=ROOT, env=env, stdout=log,
                               stderr=subprocess.STDOUT)
        print("%s  %s: %s after %.0f min" % (time.strftime("%H:%M"), s, "done" if r.returncode == 0 else
                                             "FAILED (%d)" % r.returncode, (time.time() - t) / 60), flush=True)


if __name__ == "__main__":
    main()
