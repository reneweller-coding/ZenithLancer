"""Paints the speakers' portraits locally with FLUX.2 [klein] 4B (Apache-2.0, in the Hugging Face cache) through the
AudioGenerator's Python (diffusers 0.37): a few takes per figure into work/portraits/<name>_<n>.png, the chosen one
(take 0 unless tools/portraits_pick.json says otherwise) as assets/portraits/<name>.png, 512 x 512.

    <AudioGenerator>\\venv\\Scripts\\python.exe tools\\make_portraits.py [--force] [--only NAME ...]
        [--pick]   only copy the chosen takes (after looking at them)

On the GPU (tools/gpu_queue.py). data/lang/speakers.txt says who shows which portrait.
"""
import argparse
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = os.path.join(ROOT, "work", "portraits")
OUT = os.path.join(ROOT, "assets", "portraits")
MODEL = "black-forest-labs/FLUX.2-klein-4B"
TAKES = 3

STYLE = ("painted science fiction portrait, head and shoulders, three-quarter view, dark plain background with a soft "
         "coloured rim light, painterly realism with visible brush strokes, cinematic, muted colours with one accent "
         "colour, no text, no letters, no logos, no frame, no border")

PORTRAITS = {
    "vey": "Nadia Vey, a woman test pilot in her early thirties, short dark brown hair pushed back, a thin scar on "
           "her chin, a dark grey flight suit with an orange collar, a helmet held under her arm, calm stubborn eyes, "
           "cool blue rim light. " + STYLE,
    "orrin": "A fighter craft's on-board computer seen inside the cockpit: a round glowing blue lens set flush into a "
             "dark curved instrument panel, rows of small switches and tiny displays around it, a calm point of light "
             "in the lens like an eye, no person, no robot, no helmet, no body, no head, flat front view. " + STYLE,
    "quill": "Bastian Quill, a smuggler and trader in his fifties, a broad grin, a trimmed grey-flecked beard, a "
             "battered long coat of patched burgundy leather with brass buttons, several rings, a feather tucked "
             "behind his ear, warm amber rim light. " + STYLE,
    "okonjo": "Dr. Amara Okonjo, a Black woman engineer in her early sixties, short grey locs, round wire glasses, a "
              "worn lab jacket over a dark sweater, tired brilliant eyes, a stylus behind her ear, teal rim light. "
              + STYLE,
    "salgado": "Captain Rui Salgado, a weathered sea captain in his mid-forties, salt-and-pepper stubble, wind-burnt "
               "skin, a dark oilskin coat with the collar up, sea spray on his face, a determined warm look, "
               "grey-green stormy rim light. " + STYLE,
    "voss": "Admiral Dacian Voss, a cold man in his late fifties, short silver hair, clean-shaven, pale grey eyes, an "
            "immaculate black high-collared naval uniform with silver piping and no insignia text, a controlled "
            "expression, cold white rim light. " + STYLE,
    "curator": "The Curator, the serene face of a machine mind made of small white porcelain-like hexagonal tiles "
               "with thin glowing golden seams between them, calm closed-mouth smile, gentle eyes of soft light, "
               "genderless but masculine, no hair, a faint lattice pattern fading into the dark. " + STYLE,
    "choir": "The Choir, the living mind of a jungle moon: a dense mass of glowing bioluminescent fungal filaments and "
             "mycelium threads in deep green and violet, drifting spores like stars, the vague suggestion of a calm "
             "face hidden in the pattern, no person. " + STYLE,
    "tosk": "Mirela Tosk, the forewoman of a mining cooperative, a sturdy woman in her fifties, cropped grey hair, "
            "a dusty orange work jacket with reflective stripes, a headlamp around her neck, a wry practical look, "
            "warm orange rim light. " + STYLE,
    "krenn": "Major Edda Krenn, a woman officer in her forties, blond hair in a tight bun, a dark blue military "
             "uniform without insignia text, a disciplined restrained look, cold blue rim light. " + STYLE,
    "halvard": "Captain Imre Halvard, a woman ship captain in her fifties, long grey braid, a weathered navy coat, a "
               "steady weary look, soft gold rim light. " + STYLE,
    "radio": "An old ship's radio set on a dark bulkhead: a round amber display with a waveform, chunky dials, a "
             "hanging microphone on a coiled cable, no person, no text, flat front view. " + STYLE,
    "machine": "An automated station console: a dark screen with a soft cyan pulse line, a small speaker grille, "
               "status lights, scratched metal, no person, no text, flat front view. " + STYLE,
    "directorate": "A Directorate officer seen in shadow, a man in a black high-collared uniform, his face mostly "
                   "hidden in darkness, only the jaw and the collar lit by cold white light, faceless authority. "
                   + STYLE,
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--only", nargs="*")
    ap.add_argument("--pick", action="store_true", help="only copy the chosen takes")
    a = ap.parse_args()
    from PIL import Image

    os.makedirs(WORK, exist_ok=True)
    os.makedirs(OUT, exist_ok=True)
    names = [n for n in PORTRAITS if not a.only or n in a.only]
    if not a.pick:
        todo = [(n, t) for n in names for t in range(TAKES)
                if a.force or not os.path.exists(os.path.join(WORK, "%s_%d.png" % (n, t)))]
        if todo:
            import torch
            from diffusers import Flux2KleinPipeline
            if os.name == "nt":
                import ctypes
                ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), 0x00004000)
            pipe = Flux2KleinPipeline.from_pretrained(MODEL, torch_dtype=torch.bfloat16).to("cuda")
            for n, t in todo:
                g = torch.Generator("cuda").manual_seed(4242 + 97 * t + sum(map(ord, n)))
                img = pipe(prompt=PORTRAITS[n], width=1024, height=1024, num_inference_steps=4, guidance_scale=1.0,
                           generator=g).images[0]
                img.save(os.path.join(WORK, "%s_%d.png" % (n, t)))
                print("painted", n, t, flush=True)
            del pipe
            torch.cuda.empty_cache()
    pick_path = os.path.join(ROOT, "tools", "portraits_pick.json")
    pick = {k: v for k, v in json.load(open(pick_path, encoding="utf-8")).items() if k != "_"} if os.path.exists(pick_path) else {}
    for n in names:
        src = os.path.join(WORK, "%s_%d.png" % (n, int(pick.get(n, 0))))
        if os.path.exists(src):
            Image.open(src).convert("RGB").resize((512, 512), Image.LANCZOS).save(os.path.join(OUT, n + ".png"))
    print("portraits:", ", ".join(n for n in names if os.path.exists(os.path.join(OUT, n + ".png"))))


if __name__ == "__main__":
    main()
