"""Where the local generators the asset scripts drive live on this machine. Nothing here is needed to build or play
the game; only the scripts that make music, voices and portraits ask. Each value comes from the environment, else
from tools/paths.local.json (kept out of git: {"ZENITH_AUDIOGEN": "D:/AudioGenerator", ...}), else its default:

    ZENITH_AUDIOGEN   the AudioGenerator folder (ACE-Step 1.5; its venv has numpy, scipy, soundfile, torch, diffusers)
    ZENITH_VOICELAB   the VoiceLab folder (Qwen3-TTS: env-qwen\\python.exe, models\\)
    ZENITH_GPU_FLAGS  the folder for GPU_BELEGT.txt and GPU_PAUSE.txt, which every GPU job of the machine shares
                      (default: the folder that holds this repository)
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# Child processes stay in the background: no console window pops up for them (Windows).
NO_WINDOW = 0x08000000 if os.name == "nt" else 0
_LOCAL = os.path.join(ROOT, "tools", "paths.local.json")
_DEFAULTS = {"ZENITH_GPU_FLAGS": os.path.dirname(ROOT)}


def get(name):
    """A folder by its name; exits with a hint when it is set nowhere."""
    value = os.environ.get(name)
    if not value and os.path.exists(_LOCAL):
        value = json.load(open(_LOCAL, encoding="utf-8")).get(name)
    value = value or _DEFAULTS.get(name)
    if not value:
        sys.exit("%s is not set: put it in the environment or in tools/paths.local.json (see tools/paths.py)" % name)
    return value


def audiogen():
    """The AudioGenerator folder."""
    return get("ZENITH_AUDIOGEN")


def audiogen_python():
    """The AudioGenerator's Python."""
    return os.path.join(audiogen(), "venv", "Scripts" if os.name == "nt" else "bin",
                        "python.exe" if os.name == "nt" else "python")


def voicelab():
    """The VoiceLab folder."""
    return get("ZENITH_VOICELAB")


def voicelab_python():
    """The VoiceLab's Python for Qwen3-TTS."""
    return os.path.join(voicelab(), "env-qwen", "python.exe" if os.name == "nt" else os.path.join("bin", "python"))


def gpu_flag():
    """The file that says the GPU is taken."""
    return os.path.join(get("ZENITH_GPU_FLAGS"), "GPU_BELEGT.txt")


def gpu_pause():
    """The file that holds every GPU job back."""
    return os.path.join(get("ZENITH_GPU_FLAGS"), "GPU_PAUSE.txt")
