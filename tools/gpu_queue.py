"""Runs one GPU job when the GPU is free (the user's rule: one GPU user at a time across all sessions): waits until no
game instance (KipKepler.exe, SpaceOpera.exe, DarkConcordance2 builds) has run for 60 s and no
GPU_PAUSE.txt exists, then writes GPU_BELEGT.txt (both in the folder tools/paths.py names), runs the command below normal
priority and deletes the flag afterwards.

    python tools/gpu_queue.py <command> [args...]
"""
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import paths   # noqa: E402

FLAG = paths.gpu_flag()
PAUSE = paths.gpu_pause()
GAMES = ("kipkepler.exe", "spaceopera.exe", "darkconcordance.exe", "darkconcordance2.exe", "zenithlancer.exe")


def games_running():
    out = subprocess.run(["tasklist"], capture_output=True, text=True, errors="replace",
                         creationflags=paths.NO_WINDOW).stdout.lower()
    return any(g in out for g in GAMES)


def gpu_working():
    """Whether the GPU is busy anyway (a job without the flag): more than 8 GB in use or more than half its load.
    The desktop alone uses 2 to 3 GB and a few per cent; Windows does not tell the memory per process."""
    try:
        out = subprocess.run(["nvidia-smi", "--query-gpu=utilization.gpu,memory.used", "--format=csv,noheader,nounits"],
                             capture_output=True, text=True, timeout=20, creationflags=paths.NO_WINDOW).stdout
        load, used = (float(v) for v in out.strip().splitlines()[0].split(","))
        return load > 50.0 or used > 8000.0
    except (OSError, ValueError, IndexError, subprocess.SubprocessError):
        return False


def main():
    cmd = sys.argv[1:]
    quiet_since = None
    while True:
        busy = games_running() or os.path.exists(PAUSE) or os.path.exists(FLAG) or gpu_working()
        if busy:
            quiet_since = None
        elif quiet_since is None:
            quiet_since = time.time()
        elif time.time() - quiet_since >= 60:
            # The flag made atomically: of two queues that saw the GPU free at once, only one goes.
            try:
                fd = os.open(FLAG, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
            except FileExistsError:
                quiet_since = None
                continue
            break
        time.sleep(5)
    mine = "Zenith Lancer: %s, started %s, pid %d\n" % (" ".join(os.path.basename(c) for c in cmd[:3]),
                                                       time.strftime("%H:%M"), os.getpid())
    with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as f:
        f.write(mine)
    print("GPU free, starting", flush=True)
    try:
        flags = 0x00004000 | paths.NO_WINDOW if os.name == "nt" else 0   # BELOW_NORMAL_PRIORITY_CLASS
        # The output handed on by name: a child without a console window would otherwise write into nothing.
        r = subprocess.run(cmd, creationflags=flags, stdout=sys.stdout, stderr=sys.stderr)
        sys.exit(r.returncode)
    finally:
        # Only our own flag goes (another job's, written while ours was gone, stays).
        try:
            with open(FLAG, encoding="utf-8", newline="\n") as f:
                ours = f.read() == mine
            if ours:
                os.remove(FLAG)
        except OSError:
            pass


if __name__ == "__main__":
    main()
