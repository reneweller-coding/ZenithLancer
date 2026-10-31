"""Writes the swarm fliers: four small kinds for every episode (a dart, a ring, a needle, a moth) in the episode's
colours, data/models/swarm.txt and data/enemies/swarm.txt, and the "swarm" line of every episode (data/episodes/eN.txt)
that sends them between the waves. Tyrian topped up its levels with random fliers; these do the same, so a mission
comes to 350-450 enemies. They are not in the kill ratio, pay a fifth of their score as credits and shoot rarely.

    python tools/gen_fliers.py
"""
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# body, plate, glow, second glow, shot colour
PALETTES = {
    1: ("#3a2440", "#6a3a70", "#ff60e0", "#60e8ff", "#ff60e0"),
    2: ("#1e3a50", "#2e5a78", "#60f0ff", "#ffb040", "#60f0ff"),
    3: ("#3a3430", "#6a5a48", "#ffb040", "#40e0ff", "#ffb040"),
    4: ("#2a3a24", "#4a6a38", "#ffe060", "#ff50d0", "#ffb030"),
    5: ("#3a2210", "#7a4a20", "#ffd060", "#ff50d0", "#ff50d0"),
    6: ("#26384a", "#5c6878", "#9fe8ff", "#ffb060", "#9fe8ff"),
}
# The kinds' names in each episode (German, English).
NAMES = {
    1: [("Funkenpfeil", "Spark Dart"), ("Glutring", "Ember Ring"), ("Aschenadel", "Ash Needle"), ("Schlackenmotte", "Slag Moth")],
    2: [("Gischtpfeil", "Spray Dart"), ("Brandungsring", "Surf Ring"), ("Tidennadel", "Tide Needle"), ("Sturmmotte", "Storm Moth")],
    3: [("Splitterpfeil", "Splinter Dart"), ("Geröllring", "Rubble Ring"), ("Staubnadel", "Dust Needle"), ("Erzmotte", "Ore Moth")],
    4: [("Sporenpfeil", "Spore Dart"), ("Rankenring", "Vine Ring"), ("Pollennadel", "Pollen Needle"), ("Schimmermotte", "Glimmer Moth")],
    5: [("Sonnenpfeil", "Sun Dart"), ("Flammenring", "Flame Ring"), ("Koronanadel", "Corona Needle"), ("Strahlenmotte", "Ray Moth")],
    6: [("Wolkenpfeil", "Cloud Dart"), ("Windring", "Wind Ring"), ("Echonadel", "Echo Needle"), ("Nebelmotte", "Mist Moth")],
}
# The swarm is tougher in the later episodes, like the waves (episode 6 as its enemies: half as much again).
HP = {1: 6, 2: 9, 3: 13, 4: 18, 5: 24, 6: 34}


def dart(b, p, g, g2):
    return ["hull len=20 wid=7 hgt=4 shape=dart color=%s" % b, "wing span=10 root=10 tip=3 sweep=7 z=3 thick=1 color=%s stripe=%s" % (p, g),
            "engine r=1.6 len=4 z=9 glow=%s color=%s" % (g2, b), "light r=0.9 y=2 z=-4 color=%s" % g]


def ring(b, p, g, g2):
    return ["ring r=8 thick=1.6 seg=14 color=%s group=spin spin=260" % p, "sphere r=4 color=%s" % b, "light r=1.6 y=2 color=%s" % g] + \
        [("light r=0.8 x=%g z=%g color=%s group=spin" % (x, z, g2)) for x, z in ((8, 0), (-8, 0), (0, 8), (0, -8))]


def needle(b, p, g, g2):
    return ["hull len=26 wid=4 hgt=3.5 shape=needle color=%s" % b, "fin h=4 root=8 tip=2 sweep=4 z=6 thick=0.8 color=%s" % p,
            "wing span=6 root=8 tip=1 sweep=6 z=6 thick=0.8 color=%s" % p, "engine r=1.2 len=3 z=12 glow=%s color=%s" % (g, b)]


def moth(b, p, g, g2):
    return ["hull len=12 wid=8 hgt=5 shape=blob color=%s" % b, "wing span=14 root=12 tip=8 sweep=-2 z=0 thick=0.8 color=%s stripe=%s" % (p, g2),
            "light r=1.2 y=3 color=%s" % g, "light r=0.7 x=10 z=2 y=1 color=%s" % g2]


# kind: (builder, path, speed, fire line or None)
KINDS = [
    (dart, "swoop cx=360 cy=320 dur=4.5", 240, "fire aimed every=4.0 delay=1.5 speed=210 bullet=needle color=%s cap=1 diff=3"),
    (ring, "sine amp=70 freq=0.5", 170, "fire ring n=4 every=5.0 delay=2.2 speed=130 bullet=orb color=%s cap=1 diff=4"),
    (needle, "dive turn=70", 300, None),
    (moth, "zigzag amp=90 freq=0.7", 190, "fire aimed every=4.5 delay=2.0 speed=180 bullet=orb color=%s cap=1 diff=3"),
]
SUFFIX = ["sdart", "sring", "sneedle", "smoth"]

models, enemies, swarm = [], [], {}
for ep in range(1, 7):
    b, p, g, g2, shot = PALETTES[ep]
    swarm[ep] = []
    for k, (build, path, speed, fire) in enumerate(KINDS):
        name = "e%d_%s" % (ep, SUFFIX[k])
        de, en = NAMES[ep][k]
        swarm[ep].append(name)
        models += ["", "# %s (%s): a swarm flier of episode %d." % (de, en, ep), "model " + name] + ["  part " + x for x in build(b, p, g, g2)]
        hp = HP[ep] * (1.4 if k == 1 else 0.8 if k == 2 else 1.0)
        enemies += ["", "enemy " + name, '  title "%s"' % de, '  title_en "%s"' % en, "  model " + name, "  swarm 1", "  worth 0.2",
                    "  hp %g" % round(hp, 1), "  radius %d" % (11 if k == 1 else 9), "  score %d" % (8 + 4 * ep), "  speed %d" % speed,
                    "  path " + path, "  face %s" % ("spin" if k == 1 else "move")] + \
            (["  " + fire % shot] if fire else []) + ["  drop power 0.01 mode=arcade", "  explode small", "  contact 8"]

with open(os.path.join(ROOT, "data", "models", "swarm.txt"), "w", encoding="utf-8", newline="\n") as f:
    f.write("# The swarm fliers, four kinds for every episode. Written by tools/gen_fliers.py.\n")
    f.write("\n".join(models) + "\n")
with open(os.path.join(ROOT, "data", "enemies", "swarm.txt"), "w", encoding="utf-8", newline="\n") as f:
    f.write("# The swarm fliers (swarm 1: not in the kill ratio; worth: the share of the score paid as credits). They come in\n"
            "# squads between the waves, by the episodes' \"swarm\" lines. Written by tools/gen_fliers.py.\n")
    f.write("\n".join(enemies) + "\n")

# Every episode: a squad of 3-6 about every 3.6 s while no boss holds the field (the first two a little less often:
# the pilots start there with the prototype's single gun).
EVERY = {1: 4.6, 2: 4.0, 3: 3.6, 4: 3.6, 5: 3.6, 6: 3.6}
for ep in range(1, 7):
    path = os.path.join(ROOT, "data", "episodes", "e%d.txt" % ep)
    s = open(path, encoding="utf-8", newline="").read()
    nl = "\r\n" if "\r\n" in s else "\n"
    s = re.sub(r"\r?\n  swarm [^\r\n]*", "", s)
    s = re.sub(r"(\r?\n  ending [^\r\n]*)", r"\1" + nl + "  swarm every=%g group=6 " % EVERY[ep] + "" + " ".join(swarm[ep]), s, count=1)
    open(path, "w", encoding="utf-8", newline="").write(s)
print("swarm fliers:", sum(len(v) for v in swarm.values()))
