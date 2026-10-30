"""A contact sheet of the portrait takes (work/portraits/<name>_<n>.png): one row per figure, its takes side by side,
the name above, to choose from (tools/portraits_pick.json). Writes work/portraits/sheet_<n>.png, seven figures each.

    python tools/portrait_sheet.py
"""
import glob
import os

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = os.path.join(ROOT, "work", "portraits")
SIZE = 240


def main():
    names = sorted({os.path.basename(p).rsplit("_", 1)[0] for p in glob.glob(os.path.join(WORK, "*_[0-9].png"))})
    for s in range(0, len(names), 7):
        part = names[s:s + 7]
        sheet = Image.new("RGB", (SIZE * 3, (SIZE + 24) * len(part)), (20, 20, 24))
        d = ImageDraw.Draw(sheet)
        for r, n in enumerate(part):
            d.text((6, r * (SIZE + 24) + 4), n, fill=(255, 220, 140))
            for t in range(3):
                p = os.path.join(WORK, "%s_%d.png" % (n, t))
                if os.path.exists(p):
                    sheet.paste(Image.open(p).convert("RGB").resize((SIZE, SIZE)), (t * SIZE, r * (SIZE + 24) + 24))
        out = os.path.join(WORK, "sheet_%d.png" % (s // 7))
        sheet.save(out)
        print(out)


if __name__ == "__main__":
    main()
