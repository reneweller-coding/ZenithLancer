"""Writes data/lang/speakers.txt: for every "from" of the story texts (German and English) the portrait the game shows
beside the text and the voice tools/make_voices.py speaks it with. The cast has its own; everyone else gets a fitting
general voice (a man, a woman, an officer, an automated announcement, a narrator for the places and dates of the
intros and endings) and a general portrait (a radio set, an automated console, a Directorate officer).

    python tools/speakers.py

Edit CAST and the rules below, not the file it writes.
"""
import os
import re
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import story  # noqa: E402

ROOT = story.ROOT
VOICED = ("radio", "briefing", "debrief", "intro", "ending")

# (a part of the German name, portrait, voice)
CAST = [
    ("übertragen von ORRIN", "choir", "orrin"),
    ("ORRIN", "orrin", "orrin"),
    ("Quill", "quill", "quill"),
    ("Okonjo", "okonjo", "okonjo"),
    ("Salgado", "salgado", "salgado"),
    ("Voss", "voss", "voss"),
    ("Kurator", "curator", "curator"),
    ("Das Echo", "curator", "curator"),
    ("Chor", "choir", "choir"),
    ("Nadia", "vey", "vey"),
    ("Tosk", "tosk", "tosk"),
    ("Krenn", "krenn", "krenn"),
    ("Halvard", "halvard", "halvard"),
    ("Mika, 7", "radio", "child"),
]
MACHINE = ("Durchsage", "Ansage", "Automatik", "Automatisch", "Satellit", "Sonde", "Drohne", "Bordlogbuch", "Stellwerk",
           "Kühlkeller", "Hafenmeisterei", "Hafenamt", "Verkehrsamt", "Industrieaufsicht", "Versand", "Rundfunk",
           "Werksicherheit", "Konstruktionsbüro", "Technische Abteilung", "Lancer-Programm", "Glockenstein", "Archiv")
OFFICER = ("Direktorat", "Admiralstab", "Flottenführung", "Flottenkommando", "Kreuzer", "Fregatte",
           "Besatzungsverwaltung", "Jagdstaffel", "Quarantänekommando", "Festung", "Flusspatrouille", "Brandtrupp",
           "Leutnant", "Obermaat", "Major ", "Admiral")
FEMALE_NAMES = ("Ilse", "Pia", "Lia", "Nell", "Selma", "Dalia", "Henrike", "Marisol", "Ilona", "Ada", "Mirela", "Edda",
                "Imre", "Bryn", "Tamsin", "Varga", "Ansel", "Arden")
OLD = ("Der Alte", "Archivar", "Prof.", "vor fünfzig Jahren")
# Places and dates at the head of an intro or an ending: a narrator, no portrait.
PLACE = re.compile(r"(\d{4}|nach Zenith|System|Mond des|Orbit über|^Corvina, Thalassa$|^Vael$|^Vael, )")


def classify(name):
    for part, portrait, voice in CAST:
        if part in name:
            return portrait, voice
    if not name or PLACE.search(name):
        return "-", "narrator"
    low = name.lower()
    if re.search(r"\b([1-9]|1[0-3]) Jahre", name):
        return "radio", "child"
    if any(w in name for w in OLD):
        return "radio", "oldman"
    words = re.findall(r"[\wÄÖÜäöüß]+", name)
    female = any(w in FEMALE_NAMES for w in words) or any(len(w) > 4 and w.endswith("in") and w[0].isupper() and
                                                          w not in ("Ascalon", "Kalmar") for w in words)
    two = zlib.crc32(name.encode("utf-8")) & 1
    if female:
        return "radio", "woman2" if two else "woman"
    if any(w.lower() in low for w in OFFICER):
        return "directorate", "officer"
    if any(w.lower() in low for w in MACHINE):
        return "machine", "announcer"
    return "radio", "man2" if two else "man"


def main():
    de, en = story.read("de"), story.read("en")
    names = {}
    for key, e in de.items():
        if e["type"] not in VOICED and e["type"] != "cube":
            continue
        portrait, voice = classify(e["from"])
        for n in (e["from"], en.get(key, {}).get("from", "")):
            if n:
                names.setdefault(n, (portrait, voice))
    path = os.path.join(ROOT, "data", "lang", "speakers.txt")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("# Who speaks the story texts (written by tools/speakers.py): <portrait> <voice> <the \"from\" as written>.\n"
                "# The portrait is assets/portraits/<portrait>.png (- none), the voice tools/voices.json's figure.\n")
        for n, (p, v) in sorted(names.items(), key=lambda kv: (kv[1][1], kv[0])):
            f.write("%s %s %s\n" % (p, v, n))
    print("%d names -> %s" % (len(names), path))


if __name__ == "__main__":
    main()
