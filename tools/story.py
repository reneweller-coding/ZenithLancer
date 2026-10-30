"""Reads the game's story texts (data/story/<language>/*.txt) the way Data.cpp does: a block starts at column 0 with
"<type> <key>", its lines are indented ("title ...", "from ...", "portrait ...") or prose lines starting with ">".
An empty ">" line is a paragraph break. Used by tools/speakers.py and tools/make_voices.py.
"""
import glob
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def unquote(s):
    s = s.strip()
    return s[1:-1] if len(s) >= 2 and s[0] == '"' and s[-1] == '"' else s


def read(lang):
    """{key: {"type", "title", "from", "text" (paragraphs joined by blank lines)}} of one language."""
    out = {}
    for path in sorted(glob.glob(os.path.join(ROOT, "data", "story", lang, "*.txt"))):
        cur = None
        for raw in open(path, encoding="utf-8"):
            line = raw.rstrip("\n").rstrip("\r")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            if line[0] not in " \t>":
                w = line.split()
                if len(w) >= 2:
                    cur = out.setdefault(w[1], {"type": w[0], "title": "", "from": "", "text": []})
                continue
            if cur is None:
                continue
            s = line.strip()
            if s.startswith(">"):
                cur["text"].append(s[1:].strip())
            elif s.startswith("from "):
                cur["from"] = unquote(s[5:])
            elif s.startswith("title "):
                cur["title"] = unquote(s[6:])
    for e in out.values():
        paras, cur = [], []
        for t in e["text"]:
            if t:
                cur.append(t)
            elif cur:
                paras.append(" ".join(cur))
                cur = []
        if cur:
            paras.append(" ".join(cur))
        e["text"] = "\n\n".join(paras)
    return out
