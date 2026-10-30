"""Speaks the story with Qwen3-TTS (local, G:\\Tools\\VoiceLab): every radio call, briefing, debriefing, intro and
ending of data/story/<language>, per language, in the voice of its speaker (data/lang/speakers.txt -> tools/voices.json).
The log crystals stay unspoken (documents to read).

    design   a voice per figure and language from its description (VoiceDesign 1.7B), spoken on a few of its own
             lines -> tools/voices/<figure>_<language>.flac and .txt (kept, so the figures keep their voices for texts
             written later); all of them one after the other -> work/voice/voices_<language>.wav, to listen to
    speak    every text cloned from that voice (Base 1.7B), long texts in parts of whole sentences ->
             work/voice/<language>/<key>__<n>.wav, again when a part's length does not fit its words
    encode   the parts joined (a breath between sentences, a longer pause between paragraphs), the figure's effects,
             one loudness, OGG Vorbis mono -> assets/voice/<language>/<key>.ogg

    set PYTHONNOUSERSITE=1
    G:\\Tools\\VoiceLab\\env-qwen\\python.exe tools\\make_voices.py [--lang de] [--who orrin ...] [--only KEY ...]
    G:\\Tools\\VoiceLab\\env-qwen\\python.exe tools\\make_voices.py --list

Runs below normal priority, on the GPU (tools/gpu_queue.py). Needs ffmpeg with libvorbis on the PATH.
"""
import argparse
import collections
import json
import os
import re
import sys

import numpy as np
import soundfile as sf
from scipy.signal import butter, lfilter, sosfilt

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_music  # noqa: E402  (lufs, encode)
import story  # noqa: E402

ROOT = story.ROOT
MODELS = r"G:\Tools\VoiceLab\models"
REFS = os.path.join(ROOT, "tools", "voices")
WORK = os.path.join(ROOT, "work", "voice")
LANGS = {"de": "German", "en": "English"}
VOICED = ("radio", "briefing", "debrief", "intro", "ending")

VOICE_LUFS = -16.0   # over the music and the guns
QUALITY = 3          # OGG Vorbis, mono 24 kHz: ~50 kbit/s
PEAK_DB = -2.0
BATCH = 16           # parts per generation (sorted by length, so a batch ends together)
PART = 220           # characters at most in one part (whole sentences)
SAMPLE = {"de": "Wir fliegen weiter, solange die Maschine hält. Was danach kommt, sehen wir, wenn wir dort sind.",
          "en": "We keep flying as long as the machine holds. What comes after, we will see when we get there."}


def speakers():
    """{the "from" as written: voice} of data/lang/speakers.txt."""
    out = {}
    for line in open(os.path.join(ROOT, "data", "lang", "speakers.txt"), encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        portrait, voice, name = line.rstrip("\n").split(" ", 2)
        out[name] = voice
    return out


def clean(text):
    """What is spoken of a text: no quote marks, no stage directions, shouted words in normal case."""
    text = re.sub(r"\([^)]*\)", " ", text)
    text = re.sub(r"[»«„“”\"*_]", "", text)
    text = re.sub(r"\b([A-ZÄÖÜ]{4,})\b", lambda m: m.group(1) if m.group(1) == "ORRIN" else m.group(1).capitalize(),
                  text)
    return re.sub(r"[ \t]+", " ", text).strip()


def parts(text):
    """A text in parts of whole sentences (at most PART characters), each with the pause after it in seconds."""
    out = []
    paras = [p for p in text.split("\n\n") if p.strip()]
    for pi, para in enumerate(paras):
        sentences = re.split(r"(?<=[.!?…])\s+", para.strip())
        cur = ""
        for s in sentences:
            if cur and len(cur) + 1 + len(s) > PART:
                out.append([cur, 0.3])
                cur = s
            else:
                cur = (cur + " " + s).strip()
        if cur:
            out.append([cur, 0.3])
        if out and pi < len(paras) - 1:
            out[-1][1] = 0.7
    if out:
        out[-1][1] = 0.0
    return [(t, p) for t, p in out]


def inventory():
    """Every spoken text: {(language, key): (voice, type, [(part, pause), ...])}."""
    who = speakers()
    de = story.read("de")
    out = {}
    for lang in LANGS:
        texts = de if lang == "de" else story.read(lang)
        for key, e in texts.items():
            base = de.get(key, e)
            if base["type"] not in VOICED or not e["text"].strip():
                continue
            voice = who.get(base["from"], "narrator" if not base["from"] else "man")
            ps = parts(clean(e["text"]))
            if ps:
                out[(lang, key)] = (voice, base["type"], ps)
    return out


def design_text(lines, lang):
    """A few of a figure's lines, enough for a voice to show itself (~100-200 characters, 7-15 s); a figure with
    hardly a line says a sentence of the world besides."""
    chosen, n = [], 0
    for t in sorted(set(lines), key=lambda s: -len(s)):
        if n >= 100:
            break
        if n + len(t) <= 200 or not chosen:
            chosen.append(t)
            n += len(t) + 1
    if n < 80:
        chosen.append(SAMPLE[lang])
    return " ".join(chosen)


def below_normal():
    if os.name == "nt":
        import ctypes
        ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), 0x00004000)


def plausible(wav, sr, text):
    """Whether a take's length fits its text (German and English run at about 12-16 characters a second)."""
    seconds = len(wav) / sr
    return len(text) / 30.0 + 0.3 <= seconds <= len(text) / 6.0 + 2.0


def design(jobs, voices, langs, whos):
    """A voice per figure and language that has none yet."""
    need = sorted({(v, lang) for (lang, _), (v, _, _) in jobs.items() if lang in langs and (not whos or v in whos)})
    need = [(v, lang) for v, lang in need if not os.path.exists(os.path.join(REFS, "%s_%s.flac" % (v, lang)))]
    if not need:
        return
    import torch
    from qwen_tts import Qwen3TTSModel
    model = Qwen3TTSModel.from_pretrained(os.path.join(MODELS, "Qwen3-TTS-12Hz-1.7B-VoiceDesign"), device_map="cuda:0",
                                          dtype=torch.bfloat16, attn_implementation="sdpa")
    os.makedirs(REFS, exist_ok=True)
    todo = []
    for v, lang in need:
        if "describe" not in voices.get(v, {}):
            print("no description for", v)
            continue
        lines = [p for (l, _), (w, _, ps) in jobs.items() if l == lang and w == v for p, _ in ps]
        todo.append((v, lang, design_text(lines, lang)))
    todo.sort(key=lambda d: len(d[2]))
    for attempt in range(3):
        again = []
        for i in range(0, len(todo), BATCH):
            batch = todo[i:i + BATCH]
            wavs, sr = model.generate_voice_design(text=[d[2] for d in batch],
                                                   instruct=[voices[d[0]]["describe"] for d in batch],
                                                   language=[LANGS[d[1]] for d in batch])
            for (v, lang, text), wav in zip(batch, wavs):
                if plausible(wav, sr, text) or attempt == 2:
                    sf.write(os.path.join(REFS, "%s_%s.flac" % (v, lang)), wav, sr)
                    with open(os.path.join(REFS, "%s_%s.txt" % (v, lang)), "w", encoding="utf-8") as f:
                        f.write(text + "\n")
                    print("designed", v, lang, flush=True)
                else:
                    again.append((v, lang, text))
        todo = again
    del model
    torch.cuda.empty_cache()


def listening_sheet(lang):
    """All voices of a language one after the other (a second of silence between them)."""
    parts_, rate, names = [], None, []
    for path in sorted(__import__("glob").glob(os.path.join(REFS, "*_%s.flac" % lang))):
        x, sr = sf.read(path, dtype="float32")
        rate = rate or sr
        if sr != rate:
            continue
        x = x * 10.0 ** ((VOICE_LUFS - make_music.lufs(x, sr)) / 20.0)
        parts_ += [x, np.zeros(sr, dtype=np.float32)]
        names.append(os.path.basename(path)[:-len("_%s.flac" % lang)])
    if parts_:
        os.makedirs(WORK, exist_ok=True)
        sf.write(os.path.join(WORK, "voices_%s.wav" % lang), np.concatenate(parts_), rate)
        print("voices_%s.wav: %s" % (lang, ", ".join(names)))


def speak(jobs, langs, whos, only, force):
    """Every part that has no take yet, cloned from its figure's voice."""
    groups = collections.defaultdict(list)
    for (lang, key), (v, _, ps) in jobs.items():
        if lang not in langs or (whos and v not in whos) or (only and key not in only):
            continue
        for n, (text, _) in enumerate(ps):
            raw = os.path.join(WORK, lang, "%s__%d.wav" % (key, n))
            if force and os.path.exists(raw):
                os.remove(raw)
            if not os.path.exists(raw):
                groups[(v, lang)].append(("%s__%d" % (key, n), text))
    if not groups:
        return
    import torch
    from qwen_tts import Qwen3TTSModel
    model = Qwen3TTSModel.from_pretrained(os.path.join(MODELS, "Qwen3-TTS-12Hz-1.7B-Base"), device_map="cuda:0",
                                          dtype=torch.bfloat16, attn_implementation="sdpa")
    total = sum(len(g) for g in groups.values())
    done = 0
    for (v, lang), lines in sorted(groups.items()):
        ref = os.path.join(REFS, "%s_%s.flac" % (v, lang))
        if not os.path.exists(ref):
            print("no voice for", v, lang)
            continue
        prompt = model.create_voice_clone_prompt(ref_audio=ref,
                                                 ref_text=open(ref[:-5] + ".txt", encoding="utf-8").read().strip())
        os.makedirs(os.path.join(WORK, lang), exist_ok=True)
        lines.sort(key=lambda l: len(l[1]))
        for i in range(0, len(lines), BATCH):
            todo = lines[i:i + BATCH]
            for attempt in range(4):
                if not todo:
                    break
                said = [t for _, t in todo]
                wavs, sr = model.generate_voice_clone(text=said, language=[LANGS[lang]] * len(said),
                                                      voice_clone_prompt=prompt * len(said),
                                                      max_new_tokens=int(12 * (len(max(said, key=len)) / 6.0 + 4)))
                again = []
                for (file, text), wav in zip(todo, wavs):
                    if plausible(wav, sr, text) or attempt == 3:
                        sf.write(os.path.join(WORK, lang, file + ".wav"), wav, sr)
                        done += 1
                    else:
                        again.append((file, text))
                todo = again
            print("%d/%d  %s %s" % (done, total, v, lang), flush=True)
    del model
    torch.cuda.empty_cache()


def trim(x, sr):
    """Off with the silence before and after (30 ms kept before, 120 ms after)."""
    hop = int(0.01 * sr)
    frames = np.array([np.sqrt(np.mean(x[i:i + hop] ** 2) + 1e-12) for i in range(0, max(1, len(x) - hop), hop)])
    loud = np.where(20.0 * np.log10(frames / frames.max()) > -40.0)[0]
    if not len(loud):
        return x
    return x[max(0, loud[0] * hop - int(0.03 * sr)):min(len(x), (loud[-1] + 1) * hop + int(0.12 * sr))]


def radio(x, sr):
    """A fighter's radio: telephone band, a little overdriven, a faint hiss."""
    y = sosfilt(butter(2, [300.0, 3400.0], btype="band", fs=sr, output="sos"), x)
    y = np.tanh(3.0 * y / max(1e-6, np.max(np.abs(y)))) / np.tanh(3.0)
    rng = np.random.default_rng(len(x))
    return y + rng.normal(0.0, 0.004, len(y))


def machine(x, sr):
    """The ship's mind: a light metal comb, nothing more (ORRIN must stay warm)."""
    d = int(0.0035 * sr)
    y = lfilter([1.0], np.r_[1.0, np.zeros(d - 1), -0.25], x)
    return 0.75 * x + 0.25 * y / max(1e-6, np.max(np.abs(y))) * np.max(np.abs(x))


def shimmer(x, sr):
    """The Curator: very even, with a soft second voice a fifth of a tone above and a short bright room."""
    t = np.arange(len(x)) / sr
    up = np.interp(np.arange(len(x)) * 1.012, np.arange(len(x)), x, right=0.0)
    y = 0.8 * x + 0.25 * up
    out = np.r_[y, np.zeros(int(0.25 * sr))]
    for delay, gain in ((0.031, 0.18), (0.067, 0.12), (0.113, 0.07)):
        k = int(delay * sr)
        out[k:k + len(y)] += gain * y
    return out * (0.97 + 0.03 * np.sin(2.0 * np.pi * 3.0 * np.arange(len(out)) / sr)) + 0 * t.sum()


def chorus(x, sr):
    """The Choir: the voice and three others, a little higher, lower and later, as if many spoke at once."""
    n = len(x)
    out = np.r_[x, np.zeros(int(0.2 * sr))].astype(np.float64)
    for rate, delay, gain in ((1.03, 0.021, 0.5), (0.97, 0.043, 0.5), (1.06, 0.071, 0.3)):
        y = np.interp(np.arange(n) * rate, np.arange(n), x, right=0.0)
        k = int(delay * sr)
        out[k:k + n] += gain * y
    return sosfilt(butter(2, 180.0, btype="high", fs=sr, output="sos"), out)


def pa(x, sr):
    """A loudspeaker in a hall: a narrower band and three echoes."""
    y = sosfilt(butter(2, [180.0, 7000.0], btype="band", fs=sr, output="sos"), x)
    out = np.r_[y, np.zeros(int(0.4 * sr))]
    for delay, gain in ((0.09, 0.28), (0.19, 0.17), (0.32, 0.09)):
        k = int(delay * sr)
        out[k:k + len(y)] += gain * y
    return out


def limit(x, sr, ceiling):
    """A peak limiter: the gain dips around a peak (20 ms, smoothly) instead of the whole line getting quieter."""
    from scipy.ndimage import maximum_filter1d, minimum_filter1d, uniform_filter1d
    w = int(0.02 * sr) | 1
    gain = np.minimum(1.0, 10.0 ** (ceiling / 20.0) / np.maximum(maximum_filter1d(np.abs(x), size=w), 1e-9))
    return x * uniform_filter1d(minimum_filter1d(gain, size=w), size=w)


def encode(jobs, voices, langs, force):
    """The parts joined, with their figure's effects, levelled, as OGG Vorbis."""
    n = 0
    for (lang, key), (v, kind, ps) in sorted(jobs.items()):
        if lang not in langs:
            continue
        raws = [os.path.join(WORK, lang, "%s__%d.wav" % (key, i)) for i in range(len(ps))]
        out = os.path.join(ROOT, "assets", "voice", lang, key + ".ogg")
        if not all(os.path.exists(r) for r in raws):
            continue
        if not force and os.path.exists(out) and os.path.getmtime(out) >= max(os.path.getmtime(r) for r in raws):
            continue
        pieces, sr = [], None
        for r, (_, pause) in zip(raws, ps):
            x, s = sf.read(r, dtype="float64")
            sr = sr or s
            pieces += [trim(x, s), np.zeros(int(pause * s))]
        x = np.concatenate(pieces)
        fx = list(voices.get(v, {}).get("fx", []))
        # A call in flight comes over the radio, unless the voice has its own sound (ORRIN, the Curator, the Choir).
        if kind == "radio" and not fx:
            fx = ["radio"]
        for f in fx:
            x = globals()[f](x, sr)
        for _ in range(2):   # the limiter takes a little loudness, the second pass gives it back
            x = limit(x * 10.0 ** (min(12.0, VOICE_LUFS - make_music.lufs(x, sr)) / 20.0), sr, PEAK_DB)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        make_music.encode(x.astype(np.float32), sr, out, QUALITY)
        n += 1
    print("encoded", n)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lang", nargs="*", default=list(LANGS), help="only these languages")
    ap.add_argument("--who", nargs="*", default=[], help="only these voices")
    ap.add_argument("--only", nargs="*", default=[], help="only these story keys")
    ap.add_argument("--force", action="store_true", help="speak the chosen texts again")
    ap.add_argument("--list", action="store_true", help="only count the texts per voice")
    ap.add_argument("--design-only", action="store_true", help="only design the missing voices")
    ap.add_argument("--encode-only", action="store_true", help="only encode the takes there are")
    a = ap.parse_args()
    voices = {k: v for k, v in json.load(open(os.path.join(ROOT, "tools", "voices.json"), encoding="utf-8")).items()
              if k != "_"}
    jobs = inventory()
    if a.list:
        for lang in a.lang:
            count, chars = collections.Counter(), collections.Counter()
            for (l, _), (v, _, ps) in jobs.items():
                if l == lang:
                    count[v] += 1
                    chars[v] += sum(len(p) for p, _ in ps)
            print(lang, sum(count.values()), "texts", sum(chars.values()), "characters")
            for v, n in count.most_common():
                print("  %-10s %4d texts %7d characters" % (v, n, chars[v]))
        return
    below_normal()
    if not a.encode_only:
        design(jobs, voices, a.lang, a.who)
        for lang in a.lang:
            listening_sheet(lang)
        if a.design_only:
            return
        speak(jobs, a.lang, a.who, a.only, a.force)
    encode(jobs, voices, a.lang, a.force and a.encode_only)


if __name__ == "__main__":
    main()
