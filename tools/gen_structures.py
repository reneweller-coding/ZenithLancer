"""Writes the ground's destructible structures: data/models/structures.txt (their models and the ruins they leave),
data/enemies/structures.txt (scenery: no fire, a little money, a ruin when destroyed) and the "structures" line of
every theme with a ground (which of them stand there, and how densely). Each world has its own: Pyra's foundries
chimneys, silos and halls; Thalassa's rigs, buoys and barges; the stations' containers and tanks; Sylvara's huts and
towers; the Crown's spires; Vael's skiffs and crystals."""
import math
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
models, enemies = [], []


def model(name, comment, parts):
    models.append("")
    models.append("# " + comment)
    models.append("model " + name)
    models.extend("  part " + p for p in parts)


def around(n, r, fn, start=0.0):
    out = []
    for i in range(n):
        a = math.radians(start + 360.0 * i / n)
        out += fn(round(r * math.sin(a), 2), round(-r * math.cos(a), 2), round(math.degrees(a), 1))
    return out


# ---------------------------------------------------------------------------------------------------- builders
def silo(c, roof, light, h=34, r=11):
    return ["disc r=%g h=%g seg=12 color=%s" % (r, h, c), "dome r=%g y=%g color=%s" % (r, h, roof),
            "ring r=%g thick=0.8 seg=14 y=%g color=%s" % (r + 0.6, h * 0.5, roof), "light r=1.2 y=%g x=%g color=%s" % (h + r * 0.8, 0, light),
            "box w=3 h=%g d=3 x=%g z=0 color=%s" % (h, r + 1.5, roof)]


def stack(c, band, glow, h=56):
    return ["box w=26 h=10 d=26 color=%s" % band, "disc r=6 h=%g seg=10 y=8 color=%s" % (h, c),
            "ring r=6.6 thick=0.8 seg=12 y=%g color=%s" % (h * 0.6, band), "ring r=6.6 thick=0.8 seg=12 y=%g color=%s" % (h * 0.85, band),
            "light r=3 y=%g color=%s" % (h + 9, glow), "greeble n=6 w=24 d=24 y=10 size=2 seed=3 color=%s" % band]


def hall(c, roof, trim, w=56, d=36, h=16):
    return ["box w=%g h=%g d=%g color=%s" % (w, h, d, c), "box w=%g h=6 d=%g y=%g taper=0.6 color=%s" % (w + 2, d + 2, h, roof),
            "box w=%g h=10 d=2 z=%g y=0 color=%s" % (w * 0.4, -d / 2 - 0.5, trim),
            "greeble n=8 w=%g d=%g y=%g size=2.2 seed=7 color=%s" % (w - 8, d - 8, h + 6, trim)]


def tank(c, base, light, r=13):
    return ["box w=%g h=4 d=%g color=%s" % (r * 2.2, r * 2.2, base), "sphere r=%g y=%g seg=12 color=%s" % (r, r + 3, c),
            "ring r=%g thick=0.8 seg=16 y=%g color=%s" % (r + 0.3, r + 3, base), "light r=1 y=%g color=%s" % (r * 2 + 3, light)]


def mast(c, dish, light, h=48):
    return ["box w=12 h=4 d=12 color=%s" % c, "disc r=1.4 h=%g seg=6 y=4 color=%s" % (h, c),
            "disc r=9 h=1.2 seg=14 y=%g pitch=60 color=%s" % (h * 0.7, dish), "light r=1.6 y=%g color=%s" % (h + 5, light),
            "box w=1 h=%g d=1 x=5 z=5 pitch=-8 color=%s" % (h * 0.9, c), "box w=1 h=%g d=1 x=-5 z=-5 pitch=8 color=%s" % (h * 0.9, c)]


def containers(cols, rows=2):
    out = []
    for i in range(rows):
        for j, c in enumerate(cols):
            out.append("box w=10 h=8 d=26 x=%g z=%g y=%g color=%s" % (-12 + 12 * j, 0, i * 8, cols[(i + j) % len(cols)]))
    return out


def pad(c, mark, light):
    return ["disc r=26 h=2 seg=16 color=%s" % c, "ring r=20 thick=1 seg=20 y=2 color=%s" % mark,
            "box w=20 h=0.6 d=3 y=2 color=%s" % mark, "box w=3 h=0.6 d=20 y=2 color=%s" % mark] + \
        around(8, 24, lambda x, z, a: ["light r=1 y=2.5 x=%g z=%g color=%s mirror=0" % (x, z, light)])


def crane(c, beam, light):
    return ["box w=6 h=40 d=6 x=-20 color=%s" % c, "box w=6 h=40 d=6 x=20 color=%s" % c,
            "box w=50 h=6 d=8 y=40 color=%s" % beam, "box w=8 h=6 d=8 y=34 x=6 color=%s" % c,
            "disc r=0.8 h=20 seg=6 y=14 x=6 color=%s" % c, "light r=1.4 y=47 x=-20 color=%s" % light, "light r=1.4 y=47 x=20 color=%s" % light]


def dome(c, glass, light, r=18):
    return ["disc r=%g h=4 seg=16 color=%s" % (r + 2, c), "dome r=%g y=4 glass=1 color=%s emit=0.25" % (r, glass),
            "box w=8 h=8 d=10 z=%g color=%s" % (r + 3, c)] + around(6, r + 2, lambda x, z, a: ["light r=0.9 y=4 x=%g z=%g color=%s mirror=0" % (x, z, light)])


def solar(c, panel):
    out = []
    for i in range(3):
        out.append("box w=30 h=0.8 d=10 x=0 z=%g y=6 pitch=-20 color=%s emit=0.15" % (-14 + 14 * i, panel))
        out.append("box w=1.5 h=6 d=1.5 x=0 z=%g color=%s" % (-14 + 14 * i, c))
    return out


def truck(c, cab, light):
    return ["box w=12 h=10 d=28 z=4 color=%s" % c, "box w=12 h=8 d=8 z=-14 color=%s" % cab,
            "light r=1 x=4 y=4 z=-18.5 color=%s mirror=1" % light, "greeble n=4 w=10 d=24 y=10 size=1.5 seed=9 color=%s" % cab]


def rig(c, deck, light):
    return ["box w=46 h=4 d=46 y=10 color=%s" % deck] + [("disc r=2.5 h=14 seg=8 x=%g z=%g color=%s" % (x, z, c)) for x in (-18, 18) for z in (-18, 18)] + \
        ["box w=8 h=34 d=8 y=14 x=8 z=-6 taper=0.7 color=%s" % c, "box w=14 h=8 d=12 y=14 x=-10 z=8 color=%s" % deck,
         "light r=1.6 y=50 x=8 z=-6 color=%s" % light, "disc r=8 h=1 seg=10 y=14.5 x=-12 z=-12 color=%s" % light]


def buoy(c, band, light):
    return around(3, 12, lambda x, z, a: ["sphere r=5 x=%g z=%g y=2 color=%s mirror=0" % (x, z, c),
                                          "ring r=5.2 thick=0.6 seg=10 x=%g z=%g y=2 color=%s mirror=0" % (x, z, band),
                                          "light r=1.2 x=%g z=%g y=8 color=%s mirror=0" % (x, z, light)])


def barge(c, deck, cols):
    return ["hull len=70 wid=24 hgt=8 shape=box round=2 color=%s" % c, "box w=20 h=10 d=12 y=8 z=24 color=%s" % deck] + \
        ["box w=9 h=7 d=14 x=%g z=%g y=8 color=%s" % (x, z, cols[(i) % len(cols)]) for i, (x, z) in enumerate(((-5, -20), (5, -20), (-5, -4), (5, -4)))]


def beacon(c, top, light):
    return ["disc r=12 h=4 seg=12 color=%s" % c, "disc r=5 h=34 seg=10 y=4 color=%s" % top,
            "ring r=5.5 thick=1 seg=12 y=20 color=%s" % c, "disc r=7 h=6 seg=10 y=38 color=%s" % c, "light r=3.5 y=44 color=%s" % light]


def tower(c, win, roof, h=60, w=24):
    return ["box w=%g h=%g d=%g color=%s" % (w, h, w, c), "box w=%g h=4 d=%g y=%g color=%s" % (w + 2, w + 2, h, roof),
            "greeble n=10 w=%g d=%g y=%g size=2 seed=11 color=%s" % (w - 4, w - 4, h + 4, roof)] + \
        ["box w=%g h=1.4 d=%g y=%g color=%s emit=0.6" % (w + 0.4, w + 0.4, y, win) for y in range(8, int(h) - 4, 9)]


def pylon(c, light):
    return ["box w=4 h=50 d=4 taper=0.5 color=%s" % c, "box w=30 h=2 d=3 y=44 color=%s" % c, "box w=22 h=2 d=3 y=36 color=%s" % c,
            "light r=1 x=14 y=44 color=%s mirror=1" % light]


def hut(c, roof, light):
    return [("disc r=1.5 h=10 seg=6 x=%g z=%g color=%s" % (x, z, c)) for x in (-12, 12) for z in (-10, 10)] + \
        ["box w=30 h=12 d=26 y=10 color=%s" % c, "box w=34 h=8 d=30 y=22 taper=0.4 color=%s" % roof, "light r=1.2 y=16 z=-13.5 color=%s" % light]


def pod(c, glow):
    return ["sphere r=10 rx=10 ry=14 rz=10 y=8 color=%s" % c, "sphere r=4 y=16 color=%s emit=0.9" % glow] + \
        around(5, 9, lambda x, z, a: ["spike r=1.6 len=10 x=%g z=%g y=4 pitch=50 rot=%g color=%s mirror=0" % (x, z, a, c)])


def node(c, glow):
    return ["box w=20 h=6 d=20 color=%s" % c, "crystal r=6 len=22 pitch=90 y=18 seg=6 color=%s emit=0.6" % glow,
            "ring r=12 thick=1 seg=16 y=10 color=%s emit=0.5" % glow]


def spire(c, glow, h=40):
    return ["box w=14 h=4 d=14 color=%s" % c, "crystal r=5 len=%g pitch=90 y=%g seg=6 color=%s emit=0.6" % (h, h / 2 + 4, glow),
            "crystal r=2.5 len=%g pitch=80 x=6 y=%g seg=4 rot=40 color=%s emit=0.5 mirror=1" % (h * 0.5, h * 0.3, glow)]


def relay(c, dish, glow):
    return ["box w=26 h=6 d=18 color=%s" % c] + [("disc r=8 h=1 seg=14 x=%g y=%g pitch=55 color=%s" % (x, 10, dish)) for x in (-9, 9)] + \
        ["light r=1.6 y=14 x=%g color=%s" % (x, glow) for x in (-9, 9)]


def shrine(c, glow):
    return ["disc r=14 h=4 seg=12 color=%s" % c, "ring r=12 thick=1.6 seg=20 y=16 pitch=90 color=%s emit=0.7" % glow,
            "sphere r=4 y=16 color=%s emit=1" % glow]


def skiff(c, deck, light):
    return ["hull len=50 wid=20 hgt=7 shape=cigar color=%s" % c, "box w=14 h=6 d=14 y=5 z=8 color=%s" % deck,
            "engine r=3 len=6 x=7 z=24 glow=%s color=%s mirror=1" % (light, deck), "light r=1 x=9 y=4 z=-18 color=%s mirror=1" % light]


def cluster(c, glow):
    return ["crystal r=8 len=34 pitch=80 y=14 seg=6 color=%s emit=0.3" % c, "crystal r=5 len=24 pitch=60 x=10 y=8 rot=60 seg=5 color=%s emit=0.3" % c,
            "crystal r=4 len=20 pitch=65 x=-9 z=6 y=7 rot=-120 seg=5 color=%s emit=0.3" % c, "light r=2 y=18 color=%s" % glow]


# The small things between them: drums, crates, lamps, carts; a shot or two each.
def barrels(c, band):
    return [("disc r=3.2 h=8 seg=8 x=%g z=%g color=%s" % (x, z, c)) for x, z in ((-4, -3), (3.5, -4), (0, 4), (-6, 5))] + \
        [("ring r=3.4 thick=0.5 seg=8 y=5 x=%g z=%g color=%s" % (x, z, band)) for x, z in ((-4, -3), (3.5, -4), (0, 4), (-6, 5))]


def crates(c, c2):
    return ["box w=9 h=8 d=9 x=-5 z=-3 color=%s" % c, "box w=9 h=8 d=9 x=5 z=-2 rot=10 color=%s" % c2,
            "box w=8 h=7 d=8 x=-1 z=5 rot=-15 color=%s" % c, "box w=8 h=7 d=8 x=-4 z=-2 y=8 rot=25 color=%s" % c2]


def lamp(c, light, h=24):
    return ["box w=5 h=2 d=5 color=%s" % c, "disc r=0.8 h=%g seg=6 y=2 color=%s" % (h, c),
            "box w=7 h=1.2 d=2 x=3 y=%g color=%s" % (h + 1, c), "light r=1.8 x=6 y=%g color=%s" % (h, light)]


def rover(c, cab, light, l=16):
    return ["box w=11 h=5 d=%g y=3 color=%s" % (l, c), "box w=9 h=5 d=6 y=8 z=%g color=%s" % (-l / 2 + 4, cab),
            "box w=2.5 h=4.5 d=4.5 x=6 z=%g mirror=1 color=#1e2024" % (l / 2 - 3), "box w=2.5 h=4.5 d=4.5 x=6 z=%g mirror=1 color=#1e2024" % (-l / 2 + 3),
            "light r=0.8 x=3 y=5 z=%g mirror=1 color=%s" % (-l / 2 - 0.5, light)]


def pipes(c, band):
    return ["box w=3.2 h=3.2 d=46 y=5 x=-3 color=%s" % c, "box w=3.2 h=3.2 d=46 y=5 x=3 color=%s" % c] + \
        [("box w=10 h=5 d=2 z=%g color=%s" % (z, band)) for z in (-18, 0, 18)]


def boat(c, deck, light):
    return ["hull len=24 wid=9 hgt=4 shape=tear color=%s" % c, "box w=6 h=4 d=7 y=3 z=2 color=%s" % deck, "light r=0.8 y=8 z=2 color=%s" % light]


def billboard(c, face):
    return ["box w=1.5 h=14 d=1.5 x=-8 color=%s" % c, "box w=1.5 h=14 d=1.5 x=8 color=%s" % c,
            "box w=24 h=10 d=1.2 y=14 color=%s emit=0.7" % face]


def eggs(c, glow):
    return [("sphere r=4 rx=4 ry=5.5 rz=4 x=%g z=%g y=3 color=%s" % (x, z, c)) for x, z in ((-4, -3), (4, -2), (0, 4))] + \
        [("sphere r=1.6 x=%g z=%g y=6 color=%s emit=0.9" % (x, z, glow)) for x, z in ((-4, -3), (4, -2), (0, 4))]


def shard(c, glow):
    return ["crystal r=3.2 len=16 pitch=80 y=6 seg=5 color=%s emit=0.5" % glow, "crystal r=2 len=10 pitch=55 x=4 y=3 rot=70 seg=4 color=%s emit=0.3" % c,
            "box w=10 h=2 d=10 color=%s" % c]


def kiosk(c, roof, light):
    return ["box w=12 h=8 d=10 color=%s" % c, "box w=15 h=1.5 d=13 y=8 color=%s emit=0.3" % roof, "light r=0.8 y=6 z=-5.5 color=%s" % light]


def tent(c, light):
    return ["box w=14 h=8 d=18 taper=0.95 color=%s" % c, "light r=0.8 y=3 z=-9.5 color=%s" % light]


def bulb(c, glow):
    return ["box w=8 h=2 d=8 color=%s" % c, "sphere r=5 ry=6 y=6 color=%s emit=0.6" % glow]


def spines(c):
    return ["disc r=5 h=2 seg=8 color=%s" % c] + around(5, 3, lambda x, z, a: ["spike r=1.2 len=11 x=%g z=%g y=1 pitch=40 rot=%g color=%s mirror=0" % (x, z, a, c)])


def node_small(c, glow):
    return ["box w=10 h=5 d=10 color=%s" % c, "box w=6 h=1 d=6 y=5 color=%s emit=0.9" % glow, "light r=0.8 y=7 color=%s" % glow]


# More buildings: bunkers, dishes, reactors, towers and the like, to give each world nine or so kinds.
def bunker(c, slit, w=34, d=24):
    return ["box w=%g h=10 d=%g taper=0.25 color=%s" % (w, d, c), "box w=%g h=1.6 d=1 y=5 z=%g color=%s emit=0.8" % (w * 0.6, -d / 2 + 0.6, slit),
            "box w=8 h=3 d=8 y=10 x=%g color=%s" % (w * 0.25, c), "greeble n=5 w=%g d=%g y=10 size=1.8 seed=13 color=%s" % (w * 0.6, d * 0.5, c)]


def uplink(c, dish, light, r=16):
    return ["box w=18 h=8 d=18 color=%s" % c, "disc r=2.5 h=10 seg=8 y=8 color=%s" % c,
            "disc r=%g h=1.5 seg=18 y=18 pitch=40 color=%s" % (r, dish), "disc r=0.7 h=8 seg=5 y=19 color=%s" % c, "light r=1.4 y=27 color=%s" % light]


def generator(c, coil, glow):
    return ["box w=30 h=10 d=20 color=%s" % c] + [("disc r=6 h=14 seg=12 x=%g y=10 color=%s" % (x, coil)) for x in (-8, 8)] + \
        [("ring r=6.5 thick=1 seg=14 x=%g y=%g color=%s emit=0.8" % (x, y, glow)) for x in (-8, 8) for y in (15, 21)]


def cooling(c, rim, glow, r=17):
    return ["disc r=%g h=26 seg=18 color=%s" % (r, c), "ring r=%g thick=1.2 seg=20 y=26 color=%s" % (r, rim),
            "ring r=%g thick=0.8 seg=20 y=8 color=%s" % (r + 0.3, rim), "disc r=%g h=0.6 seg=16 y=26 color=%s emit=0.25" % (r - 3, glow)]


def refinery(c, pipe, light):
    out = []
    for x in (-14, 14):
        out += ["disc r=8 h=30 seg=12 x=%g color=%s" % (x, c), "dome r=8 y=30 x=%g color=%s" % (x, pipe), "light r=1.4 y=39 x=%g color=%s" % (x, light)]
    return out + ["box w=20 h=2.5 d=2.5 y=22 color=%s" % pipe, "box w=20 h=2.5 d=2.5 y=12 z=4 color=%s" % pipe, "box w=12 h=7 d=10 z=14 color=%s" % pipe]


def furnace(c, hot, glow):
    return ["box w=30 h=14 d=30 color=%s" % c, "box w=20 h=18 d=20 y=14 taper=0.3 color=%s" % c,
            "disc r=5 h=16 seg=10 y=30 color=%s" % c, "box w=12 h=6 d=1 y=4 z=-15.2 color=%s emit=1" % hot, "light r=3 y=48 color=%s" % glow]


def house(c, roof, light, w=22, d=16):
    return ["box w=%g h=10 d=%g color=%s" % (w, d, c), "box w=%g h=7 d=%g y=10 taper=0.9 color=%s" % (w + 2, d + 2, roof),
            "light r=0.9 y=6 z=%g color=%s" % (-d / 2 - 0.4, light)]


def canopy(c, roof, light):
    return ["box w=34 h=1.5 d=20 y=14 color=%s emit=0.1" % roof] + \
        [("box w=34.4 h=0.6 d=2.4 y=15.2 z=%g color=%s emit=0.6" % (z, light)) for z in (-8.8, 8.8)] + \
        [("box w=1.5 h=14 d=1.5 x=%g z=%g color=%s" % (x, z, c)) for x in (-14, 14) for z in (-7, 7)] + \
        [("box w=3 h=6 d=3 x=%g color=%s" % (x, c)) for x in (-6, 6)] + [("light r=1 x=%g y=13 color=%s" % (x, light)) for x in (-10, 0, 10)]


def dock(c, deck, cols):
    return ["box w=12 h=2 d=70 y=6 color=%s" % deck] + [("disc r=1.5 h=8 seg=6 x=%g z=%g color=%s" % (x, z, c)) for x in (-5, 5) for z in (-30, -10, 10, 30)] + \
        [("box w=8 h=6 d=8 y=8 z=%g rot=%g color=%s" % (z, r, cols[i % len(cols)])) for i, (z, r) in enumerate(((-24, 5), (-14, -10), (18, 15)))]


def farm(c, net, light):
    pens = [(x, z) for x in (-11, 11) for z in (-11, 11)]
    return [("ring r=9 thick=1.2 seg=16 x=%g z=%g y=1 color=%s" % (x, z, c)) for x, z in pens] + \
        [("disc r=8 h=0.4 seg=14 x=%g z=%g color=%s" % (x, z, net)) for x, z in pens] + ["box w=6 h=4 d=6 y=1 color=%s" % c, "light r=1 y=6 color=%s" % light]


def tanker(c, deck, tanks):
    return ["hull len=90 wid=22 hgt=8 shape=box round=2 color=%s" % c, "box w=18 h=10 d=12 y=8 z=34 color=%s" % deck] + \
        [("sphere r=7 y=9 z=%g color=%s" % (z, tanks)) for z in (-30, -15, 0, 15)]


def drill(c, frame, light):
    return ["box w=24 h=6 d=24 color=%s" % c, "box w=10 h=46 d=10 y=6 taper=0.6 color=%s" % frame,
            "box w=10 h=10 d=14 x=12 color=%s" % c, "disc r=3 h=8 seg=8 z=-14 color=%s" % frame, "light r=1.6 y=54 color=%s" % light]


def igloo(c, shell, light, r=16):
    return ["disc r=%g h=3 seg=16 color=%s" % (r + 2, c), "dome r=%g y=3 color=%s" % (r, shell), "ring r=%g thick=0.8 seg=18 y=9 color=%s" % (r * 0.85, c),
            "box w=8 h=7 d=10 z=%g color=%s" % (r + 3, c), "light r=1 y=8 z=%g color=%s" % (r + 8.5, light)]


def garage(c, gap, light, w=40, d=28):
    return [("box w=%g h=%g d=%g y=%g color=%s" % (w, 3 if i % 2 == 0 else 3.5, d - (0 if i % 2 == 0 else 3), y, c if i % 2 == 0 else gap))
            for i, y in enumerate((0, 3, 6.5, 9.5, 13, 16))] + [("light r=1 x=%g y=19.5 z=%g color=%s" % (x, z, light)) for x in (-14, 14) for z in (-9, 9)]


def treehouse(c, roof, light):
    return [("disc r=1.6 h=16 seg=6 x=%g z=%g color=%s" % (x, z, c)) for x, z in ((-10, -8), (10, -8), (0, 10))] + \
        ["disc r=15 h=2 seg=12 y=16 color=%s" % c, "box w=14 h=8 d=12 y=18 color=%s" % c, "box w=18 h=6 d=16 y=26 taper=0.7 color=%s" % roof,
         "light r=1 y=22 z=-6.5 color=%s" % light]


def stele(c, glyph):
    return ["box w=16 h=4 d=16 color=%s" % c, "box w=8 h=30 d=6 y=4 taper=0.15 color=%s" % c, "box w=4 h=12 d=6.4 y=14 color=%s emit=0.7" % glyph,
            "greeble n=6 w=14 d=14 y=4 size=1.6 seed=17 color=%s" % c]


def mound(c, glow):
    return ["sphere r=14 ry=8 color=%s" % c, "sphere r=10 ry=9 y=10 color=%s" % c, "sphere r=6 ry=8 y=20 color=%s" % c,
            "sphere r=2.5 y=28 color=%s emit=1" % glow] + around(4, 10, lambda x, z, a: ["sphere r=1.6 x=%g z=%g y=10 color=%s emit=0.8 mirror=0" % (x, z, glow)])


def vent(c, glow):
    return ["disc r=14 h=3 seg=14 color=%s" % c, "ring r=8 thick=3 seg=14 y=4 color=%s" % c, "disc r=6 h=1 seg=12 y=4 color=%s emit=1" % glow] + \
        around(5, 13, lambda x, z, a: ["spike r=1.8 len=8 x=%g z=%g y=2 pitch=60 rot=%g color=%s mirror=0" % (x, z, a, c)])


def chrysalis(c, glow):
    return ["box w=18 h=3 d=8 color=%s" % c, "sphere r=8 rx=9 ry=7 rz=20 y=7 color=%s" % c, "sphere r=3 rx=4 ry=3 rz=10 y=12 color=%s emit=0.7" % glow]


def anemone(c, glow):
    return ["disc r=6 h=6 seg=10 color=%s" % c] + \
        around(7, 4, lambda x, z, a: ["spike r=1.6 len=16 x=%g z=%g y=4 pitch=35 rot=%g color=%s mirror=0" % (x, z, a, c)]) + ["sphere r=3 y=7 color=%s emit=1" % glow]


def gate(c, glow):
    return ["box w=12 h=3 d=12 x=-16 color=%s" % c, "box w=12 h=3 d=12 x=16 color=%s" % c,
            "box w=6 h=30 d=6 x=-16 color=%s" % c, "box w=6 h=30 d=6 x=16 color=%s" % c, "box w=38 h=4 d=6 y=30 color=%s" % c,
            "box w=26 h=1.5 d=1.5 y=16 color=%s emit=1" % glow, "box w=26 h=1.5 d=1.5 y=8 color=%s emit=1" % glow]


def column(c, glow, h=44):
    return ["box w=14 h=4 d=14 color=%s" % c, "box w=8 h=%g d=8 y=4 taper=0.4 color=%s" % (h, c)] + \
        [("ring r=6 thick=0.8 seg=8 y=%g color=%s emit=0.8" % (y, glow)) for y in (12, 22, 32)] + ["light r=2 y=%g color=%s" % (h + 6, glow)]


def core(c, glow):
    return ["box w=24 h=6 d=24 color=%s" % c, "sphere r=8 y=14 color=%s emit=0.8" % glow, "ring r=12 thick=1.2 seg=18 y=14 color=%s" % c] + \
        [("box w=3 h=18 d=3 x=%g z=%g color=%s" % (x, z, c)) for x in (-10, 10) for z in (-10, 10)]


def fins(c, glow):
    return ["box w=34 h=4 d=22 color=%s" % c] + [("box w=1.4 h=14 d=20 x=%g y=4 color=%s" % (x, c)) for x in range(-14, 15, 4)] + \
        ["box w=30 h=1 d=1 y=4.5 z=-11.2 color=%s emit=1" % glow]


def aerostat(c, env, light):
    return ["sphere r=16 rx=14 ry=12 rz=22 y=24 color=%s" % env, "box w=1.5 h=1 d=44 y=36 color=%s" % c,
            "box w=29 h=1 d=1.5 y=36 color=%s" % c, "box w=12 h=5 d=18 y=8 color=%s" % c,
            "disc r=0.6 h=10 seg=4 y=8 x=4 color=%s" % c, "light r=1.2 y=37 color=%s" % light]


def scoop(c, rim, glow):
    return ["disc r=16 h=3 seg=16 color=%s" % c, "ring r=12 thick=2.5 seg=18 y=6 color=%s" % rim, "disc r=10 h=0.6 seg=14 y=5 color=%s emit=0.7" % glow,
            "box w=6 h=12 d=6 x=14 color=%s" % c]


def ringdock(c, arm, light):
    return ["ring r=20 thick=2.5 seg=24 y=2 color=%s" % c, "box w=8 h=4 d=40 y=1 color=%s" % arm] + \
        around(6, 20, lambda x, z, a: ["light r=1 y=5 x=%g z=%g color=%s mirror=0" % (x, z, light)])


# ---------------------------------------------------------------------------------------------------- the worlds
# (name, title, title_en, builder parts, hp, score, ruin)
WORLDS = {
    "station": [
        ("st_containers", "Frachtcontainer", "Cargo Containers", containers(["#6a7a8a", "#a05a3a", "#4a6a5a"]), 60, 70, "ruin_metal"),
        ("st_tank", "Treibstofftank", "Fuel Tank", tank("#9aa4b4", "#4a5260", "#ff5040"), 70, 90, "ruin_metal"),
        ("st_mast", "Antennenmast", "Antenna Mast", mast("#7a8494", "#b0bac8", "#ff5040"), 40, 70, "ruin_metal"),
        ("st_hangar", "Hangar", "Hangar", hall("#5a6474", "#3a424e", "#8a96a8"), 120, 140, "ruin_rubble"),
        ("st_pad", "Landefeld", "Landing Pad", pad("#3a424e", "#d8c040", "#60c0ff"), 80, 80, "ruin_metal"),
    ],
    "foundry": [
        ("fo_stack", "Schlot", "Smokestack", stack("#5a4a40", "#3a2e28", "#ff8030"), 90, 110, "ruin_rubble"),
        ("fo_silo", "Erzsilo", "Ore Silo", silo("#7a6a5a", "#4a3c34", "#ff9040"), 80, 100, "ruin_rubble"),
        ("fo_hall", "Gießhalle", "Casting Hall", hall("#4a3c34", "#2e2620", "#ff7020"), 140, 160, "ruin_rubble"),
        ("fo_tank", "Schlackentank", "Slag Tank", tank("#6a5a4a", "#3a2e28", "#ff6020"), 70, 90, "ruin_metal"),
        ("fo_crane", "Portalkran", "Gantry Crane", crane("#6a5a40", "#c09030", "#ff6020"), 100, 120, "ruin_metal"),
    ],
    "desert": [
        ("de_dome", "Siedlerkuppel", "Settler Dome", dome("#9a8a6a", "#c8e0f0", "#ffd080", 16), 70, 90, "ruin_rubble"),
        ("de_tower", "Wasserturm", "Water Tower", silo("#8a7a5a", "#6a5a40", "#80c0ff", 26, 10), 60, 80, "ruin_rubble"),
        ("de_mast", "Funkmast", "Radio Mast", mast("#8a7a6a", "#c8bca8", "#ff5040"), 40, 70, "ruin_metal"),
        ("de_truck", "Erztransporter", "Ore Hauler", truck("#a07a40", "#6a5a4a", "#ffe0a0"), 50, 80, "ruin_metal"),
        ("de_solar", "Solarfeld", "Solar Field", solar("#5a5040", "#3a5a8a"), 50, 70, "ruin_metal"),
    ],
    "ocean": [
        ("oc_rig", "Bohrinsel", "Drilling Rig", rig("#6a7480", "#4a5260", "#ff5040"), 140, 160, "ruin_wreck"),
        ("oc_buoy", "Bojenfeld", "Buoy Field", buoy("#c04030", "#e8e8e8", "#ffd060"), 40, 60, "ruin_wreck"),
        ("oc_barge", "Containerkahn", "Container Barge", barge("#3a4a5a", "#5a6474", ["#a05a3a", "#4a7a5a", "#c0a040"]), 110, 130, "ruin_wreck"),
        ("oc_beacon", "Leuchtfeuer", "Beacon", beacon("#6a7480", "#d8d8d8", "#ffe080"), 60, 80, "ruin_wreck"),
    ],
    "ice": [
        ("ic_dome", "Forschungskuppel", "Research Dome", dome("#8a96a8", "#d0f0ff", "#60c0ff", 18), 80, 100, "ruin_rubble"),
        ("ic_radar", "Radarschüssel", "Radar Dish", mast("#9aa4b4", "#e8f0f8", "#ff5040", 30), 50, 80, "ruin_metal"),
        ("ic_tank", "Brennstofflager", "Fuel Depot", tank("#b0bac8", "#5a6474", "#ff8040"), 70, 90, "ruin_metal"),
        ("ic_containers", "Vorratslager", "Supply Stack", containers(["#c04030", "#5a6a8a", "#d8d8d8"]), 60, 70, "ruin_metal"),
    ],
    "city": [
        ("ci_tower", "Wohnturm", "Tower Block", tower("#5a6474", "#ffd890", "#3a424e"), 160, 180, "ruin_rubble"),
        ("ci_tower2", "Bürohaus", "Office Block", tower("#4a5a6a", "#a0e0ff", "#2e3640", 44, 30), 130, 150, "ruin_rubble"),
        ("ci_tanks", "Dachtanks", "Rooftop Tanks", tank("#8a96a8", "#4a5260", "#ff5040", 10) + ["box w=26 h=6 d=26 y=-4 color=#3a424e"], 60, 80, "ruin_metal"),
        ("ci_pylon", "Strommast", "Power Pylon", pylon("#6a7480", "#ff5040"), 40, 60, "ruin_metal"),
    ],
    "jungle": [
        ("ju_hut", "Forschungshütte", "Research Hut", hut("#6a5a40", "#3a5a30", "#ffd080"), 60, 80, "ruin_rubble"),
        ("ju_tower", "Funkturm", "Radio Tower", mast("#5a6a4a", "#a0b090", "#ff5040", 54), 50, 80, "ruin_metal"),
        ("ju_green", "Gewächshaus", "Greenhouse", dome("#4a5a40", "#a0ffb0", "#80ff90", 18), 80, 100, "ruin_rubble"),
        ("ju_silo", "Biosilo", "Bio Silo", silo("#5a6a4a", "#3a4a30", "#80ff90"), 70, 90, "ruin_rubble"),
    ],
    "hive": [
        ("hv_pod", "Sporenkapsel", "Spore Pod", pod("#8a5a90", "#70f060"), 60, 90, "ruin_crystal"),
        ("hv_node", "Gitterknoten", "Lattice Knot", node("#6a4c72", "#ff60d0"), 90, 110, "ruin_crystal"),
        ("hv_hut", "Verwachsene Hütte", "Overgrown Hut", hut("#7a5a78", "#8a5a90", "#70f060"), 60, 80, "ruin_rubble"),
    ],
    "lattice": [
        ("la_spire", "Kristallnadel", "Crystal Needle", spire("#4e4272", "#ff4fd0"), 70, 100, "ruin_crystal"),
        ("la_relay", "Gitterrelais", "Lattice Relay", relay("#4e4272", "#e8dcc0", "#ffd060"), 90, 110, "ruin_metal"),
        ("la_shrine", "Lichtring", "Light Ring", shrine("#4e4272", "#ffd060"), 80, 100, "ruin_crystal"),
    ],
    "corona": [
        ("co_spire", "Sonnennadel", "Sun Needle", spire("#6a3c1e", "#ffd060", 46), 80, 110, "ruin_crystal"),
        ("co_shrine", "Glutring", "Ember Ring", shrine("#6a3c1e", "#ff8040"), 80, 100, "ruin_crystal"),
        ("co_relay", "Kronenrelais", "Crown Relay", relay("#6a3c1e", "#ffe8a0", "#ff4fd0"), 90, 110, "ruin_metal"),
    ],
    "vael": [
        ("v6_skiff", "Wolkenbarke", "Cloud Skiff", skiff("#8a96a8", "#5c6878", "#7ab8ff"), 80, 110, "ruin_wreck"),
        ("v6_buoy", "Wetterboje", "Weather Buoy", beacon("#5c6878", "#c8d0dc", "#9fe8ff"), 60, 90, "ruin_wreck"),
        ("v6_mast", "Messplattform", "Sensor Platform", pad("#4a5260", "#9fe8ff", "#7ab8ff") + mast("#8a96a8", "#c8d0dc", "#ff5040", 30), 90, 110, "ruin_metal"),
    ],
    "deep": [
        ("v6_cluster", "Echokristalle", "Echo Crystals", cluster("#40607a", "#9fe8ff"), 90, 120, "ruin_crystal"),
        ("v6_pod", "Chorkapsel", "Choir Pod", pod("#5e4a30", "#ffb060"), 60, 90, "ruin_crystal"),
    ],
}

# The small things of each world: (name, title, title_en, parts, hp, score); they leave a scorch mark.
SMALL = {
    "station": [("st_drums", "Fässer", "Drums", barrels("#8a6a3a", "#3a3e44"), 16, 30),
                ("st_crates", "Frachtkisten", "Cargo Crates", crates("#6a7a8a", "#8a7a5a"), 18, 30),
                ("st_lamp", "Deckleuchte", "Deck Lamp", lamp("#4a5260", "#9fd8ff"), 12, 25),
                ("st_cart", "Frachtkarre", "Cargo Cart", rover("#c09030", "#4a5260", "#ffe0a0"), 20, 35)],
    "foundry": [("fo_drums", "Schlackefässer", "Slag Drums", barrels("#5a4a3a", "#ff7020"), 16, 30),
                ("fo_pipes", "Rohrleitung", "Pipeline", pipes("#6a5a4a", "#3a2e28"), 22, 35),
                ("fo_ingots", "Barrenstapel", "Ingot Stack", crates("#8a7050", "#c09060"), 18, 30)],
    "desert": [("de_drums", "Wasserfässer", "Water Drums", barrels("#6a7a8a", "#c8bca8"), 16, 30),
               ("de_rover", "Wüstenbuggy", "Dune Buggy", rover("#b08a50", "#5a5040", "#ffe0a0", 14), 20, 35),
               ("de_crates", "Vorratskisten", "Supply Crates", crates("#8a7a5a", "#6a5a40"), 18, 30)],
    "ocean": [("oc_boat", "Fischerboot", "Fishing Boat", boat("#d8d0c0", "#a05a3a", "#ffe080"), 18, 35),
              ("oc_float", "Leuchtboje", "Light Buoy", lamp("#c04030", "#ffd060", 12) + ["sphere r=4 y=1 color=#c04030"], 12, 25)],
    "ice": [("ic_cat", "Schneeraupe", "Snowcat", rover("#c04030", "#d8d8d8", "#ffe0a0"), 20, 35),
            ("ic_drums", "Brennstofffässer", "Fuel Drums", barrels("#c0a040", "#3a3e44"), 16, 30),
            ("ic_lamp", "Wegleuchte", "Path Light", lamp("#5a6474", "#ff8040", 18), 12, 25)],
    "city": [("ci_car", "Schwebewagen", "Hover Car", rover("#4a6a8a", "#a0e0ff", "#ffe0a0", 14), 18, 30),
             ("ci_sign", "Leuchtreklame", "Neon Sign", billboard("#3a424e", "#ff4fd0"), 14, 30),
             ("ci_lamp", "Straßenlaterne", "Street Lamp", lamp("#4a5260", "#ffd890", 26), 12, 25)],
    "jungle": [("ju_crates", "Probenkisten", "Sample Crates", crates("#6a5a40", "#4a5a30"), 18, 30),
               ("ju_lamp", "Lagerleuchte", "Camp Light", lamp("#5a4a30", "#ffd080", 16), 12, 25),
               ("ju_drums", "Treibstofffässer", "Fuel Drums", barrels("#4a5a30", "#c0a040"), 16, 30)],
    "hive": [("hv_eggs", "Gelege", "Clutch", eggs("#8a5a90", "#70f060"), 16, 35),
             ("hv_shard", "Gitterdorn", "Lattice Thorn", shard("#6a4c72", "#ff60d0"), 14, 30)],
    "lattice": [("la_shard", "Splitterkristall", "Shard Crystal", shard("#4e4272", "#ff4fd0"), 14, 30),
                ("la_lamp", "Gitterlicht", "Lattice Light", lamp("#4e4272", "#ffd060", 20), 12, 25)],
    "corona": [("co_shard", "Glutkristall", "Ember Crystal", shard("#6a3c1e", "#ffb040"), 14, 30),
               ("co_lamp", "Kronenlicht", "Crown Light", lamp("#6a3c1e", "#ff4fd0", 20), 12, 25)],
    "vael": [("v6_boat", "Wolkenboot", "Cloud Boat", boat("#8a96a8", "#5c6878", "#9fe8ff"), 18, 35),
             ("v6_float", "Windboje", "Wind Buoy", lamp("#5c6878", "#9fe8ff", 12) + ["sphere r=4 y=1 color=#8a96a8"], 12, 25)],
    "deep": [("v6_shard", "Echosplitter", "Echo Shard", shard("#40607a", "#9fe8ff"), 14, 30),
             ("v6_eggs", "Chorgelege", "Choir Clutch", eggs("#5e4a30", "#ffb060"), 16, 35)],
}

# More kinds for every world, so that its ground has nine or so buildings and four or five small things.
MORE = {
    "station": [("st_uplink", "Sendeschüssel", "Uplink Dish", uplink("#7a8494", "#c8d0dc", "#ff5040"), 70, 90, "ruin_metal"),
                ("st_bunker", "Lagerbunker", "Storage Bunker", bunker("#4a5260", "#ffd060"), 110, 130, "ruin_rubble"),
                ("st_reactor", "Reaktorblock", "Reactor Block", generator("#5a6474", "#3a424e", "#60c0ff"), 100, 120, "ruin_metal"),
                ("st_fuel", "Tankstelle", "Fuel Station", canopy("#4a5260", "#8a96a8", "#ffe0a0"), 80, 100, "ruin_metal")],
    "foundry": [("fo_cooling", "Kühlturm", "Cooling Tower", cooling("#6a5a4a", "#3a2e28", "#ff9040"), 130, 150, "ruin_rubble"),
                ("fo_refinery", "Raffinerie", "Refinery", refinery("#7a6a5a", "#4a3c34", "#ff8030"), 110, 130, "ruin_metal"),
                ("fo_furnace", "Hochofen", "Blast Furnace", furnace("#4a3c34", "#ff7020", "#ff8030"), 140, 160, "ruin_rubble"),
                ("fo_bunker", "Schlackenbunker", "Slag Bunker", bunker("#5a4a40", "#ff6020"), 100, 120, "ruin_rubble")],
    "desert": [("de_house", "Lehmhaus", "Adobe House", house("#b0906a", "#8a6a4a", "#ffd080"), 60, 80, "ruin_rubble"),
               ("de_bunker", "Sturmbunker", "Storm Shelter", bunker("#8a7a5a", "#ffd080"), 90, 110, "ruin_rubble"),
               ("de_uplink", "Funkschüssel", "Dish Antenna", uplink("#8a7a6a", "#e0d8c8", "#ff5040"), 60, 80, "ruin_metal"),
               ("de_fuel", "Tankstelle", "Fuel Stop", canopy("#6a5a4a", "#c0a070", "#ffe0a0"), 70, 90, "ruin_metal")],
    "ocean": [("oc_dock", "Pier", "Pier", dock("#4a4038", "#6a5a48", ["#a05a3a", "#4a7a5a"]), 90, 110, "ruin_wreck"),
              ("oc_farm", "Fischfarm", "Fish Farm", farm("#c0a040", "#1a3a4a", "#ffd060"), 70, 90, "ruin_wreck"),
              ("oc_tanker", "Tanker", "Tanker", tanker("#4a3a3a", "#6a7480", "#c0c8d0"), 150, 170, "ruin_wreck"),
              ("oc_platform", "Wohnplattform", "Habitat Platform", pad("#4a5260", "#d8c040", "#60c0ff") + house("#8a96a8", "#4a5260", "#ffe0a0"),
               120, 140, "ruin_wreck")],
    "ice": [("ic_drill", "Eisbohrer", "Ice Drill", drill("#5a6474", "#c04030", "#ff8040"), 110, 130, "ruin_metal"),
            ("ic_igloo", "Wohnkuppel", "Habitat Dome", igloo("#b0bac8", "#d8e4f0", "#60c0ff"), 80, 100, "ruin_rubble"),
            ("ic_hall", "Forschungshalle", "Research Hall", hall("#8a96a8", "#5a6474", "#60c0ff"), 120, 140, "ruin_rubble"),
            ("ic_reactor", "Wärmereaktor", "Heat Reactor", generator("#7a8494", "#4a5260", "#ff8040"), 100, 120, "ruin_metal")],
    "city": [("ci_tower3", "Hochhaus", "High-Rise", tower("#6a7484", "#ffe0a0", "#3a424e", 80, 18), 170, 190, "ruin_rubble"),
             ("ci_mall", "Markthalle", "Market Hall", hall("#5a5a6a", "#3a3a4a", "#ff4fd0", 64, 40, 14), 130, 150, "ruin_rubble"),
             ("ci_garage", "Parkhaus", "Parking Deck", garage("#7a8494", "#2e3640", "#ffd890"), 120, 140, "ruin_rubble"),
             ("ci_house", "Stadthaus", "Townhouse", house("#7a6a5a", "#4a3a34", "#ffd890", 26, 20), 70, 90, "ruin_rubble"),
             ("ci_arena", "Arena", "Arena", dome("#5a6474", "#a0e0ff", "#ffd890", 22), 140, 160, "ruin_rubble")],
    "jungle": [("ju_platform", "Baumplattform", "Tree Platform", treehouse("#5a4a30", "#3a5a30", "#ffd080"), 70, 90, "ruin_rubble"),
               ("ju_stele", "Alte Stele", "Ancient Stele", stele("#6a6a5a", "#80ff90"), 90, 110, "ruin_rubble"),
               ("ju_lab", "Feldlabor", "Field Lab", hall("#5a6a4a", "#3a4a30", "#ffd080", 44, 28, 12), 100, 120, "ruin_rubble"),
               ("ju_pad", "Landeplatz", "Landing Pad", pad("#4a4a3a", "#d8c040", "#80ff90"), 80, 90, "ruin_metal")],
    "hive": [("hv_mound", "Brutturm", "Brood Tower", mound("#8a5a90", "#70f060"), 110, 130, "ruin_crystal"),
             ("hv_vent", "Sporenschlot", "Spore Vent", vent("#7a5a78", "#ff60d0"), 80, 100, "ruin_crystal"),
             ("hv_chrysalis", "Puppe", "Chrysalis", chrysalis("#6a4c72", "#70f060"), 70, 90, "ruin_crystal"),
             ("hv_anemone", "Fangkrone", "Snare Crown", anemone("#8a5a90", "#ff60d0"), 70, 90, "ruin_crystal")],
    "lattice": [("la_gate", "Gittertor", "Lattice Gate", gate("#4e4272", "#ff4fd0"), 100, 120, "ruin_metal"),
                ("la_prism", "Prismenfeld", "Prism Field", cluster("#4e4272", "#ffd060"), 90, 110, "ruin_crystal"),
                ("la_column", "Gittersäule", "Lattice Column", column("#4e4272", "#60e0ff"), 80, 100, "ruin_crystal"),
                ("la_core", "Speicherkern", "Memory Core", core("#4e4272", "#ff4fd0"), 120, 140, "ruin_crystal"),
                ("la_mirrors", "Spiegelfeld", "Mirror Field", solar("#4e4272", "#c0a0ff"), 70, 90, "ruin_metal")],
    "corona": [("co_mirrors", "Sonnenspiegel", "Sun Mirrors", solar("#6a3c1e", "#ffd060"), 70, 90, "ruin_metal"),
               ("co_fins", "Kühlrippen", "Heat Fins", fins("#6a3c1e", "#ff8040"), 90, 110, "ruin_metal"),
               ("co_gate", "Kronentor", "Crown Gate", gate("#6a3c1e", "#ffd060"), 100, 120, "ruin_metal"),
               ("co_core", "Fusionskern", "Fusion Core", core("#6a3c1e", "#ffb040"), 120, 140, "ruin_crystal")],
    "vael": [("v6_aerostat", "Aerostat", "Aerostat", aerostat("#5c6878", "#c8d0dc", "#ff5040"), 90, 110, "ruin_wreck"),
             ("v6_scoop", "Gassammler", "Gas Scoop", scoop("#5c6878", "#8a96a8", "#9fe8ff"), 100, 120, "ruin_wreck"),
             ("v6_dock", "Andockring", "Docking Ring", ringdock("#5c6878", "#8a96a8", "#7ab8ff"), 110, 130, "ruin_wreck"),
             ("v6_tanker", "Gastanker", "Gas Tanker", tanker("#5c6878", "#8a96a8", "#c8d0dc"), 140, 160, "ruin_wreck")],
    "deep": [("v6_anemone", "Echokrone", "Echo Crown", anemone("#40607a", "#9fe8ff"), 70, 90, "ruin_crystal"),
             ("v6_vent", "Echoschlot", "Echo Vent", vent("#5e4a30", "#ffb060"), 80, 100, "ruin_crystal"),
             ("v6_spire", "Korallennadel", "Coral Needle", spire("#40607a", "#9fe8ff", 44), 80, 110, "ruin_crystal"),
             ("v6_mound", "Chorhügel", "Choir Mound", mound("#5e4a30", "#ffb060"), 110, 130, "ruin_crystal"),
             ("v6_column", "Klangsäule", "Sound Column", column("#40607a", "#ffb060"), 80, 100, "ruin_crystal")],
}
MORE_SMALL = {
    "station": [("st_beacon", "Landebake", "Landing Beacon", lamp("#4a5260", "#ffd060", 14), 12, 25)],
    "foundry": [("fo_cart", "Erzlore", "Ore Cart", rover("#5a4a40", "#8a7050", "#ff9040", 14), 20, 35),
                ("fo_lamp", "Warnleuchte", "Warning Light", lamp("#5a4a40", "#ff6020", 18), 12, 25)],
    "desert": [("de_lamp", "Signalleuchte", "Signal Light", lamp("#6a5a4a", "#ff5040", 16), 12, 25),
               ("de_pipes", "Wasserleitung", "Water Main", pipes("#8a7a6a", "#5a5040"), 22, 35)],
    "ocean": [("oc_crates", "Treibgut", "Flotsam", crates("#8a6a4a", "#5a6474"), 16, 30),
              ("oc_skiff", "Schnellboot", "Speedboat", boat("#c04030", "#d8d8d8", "#ffe080"), 18, 35)],
    "ice": [("ic_crates", "Vorratskisten", "Supply Crates", crates("#c04030", "#8a96a8"), 18, 30),
            ("ic_sled", "Lastschlitten", "Cargo Sled", rover("#5a6474", "#c0a040", "#ffe0a0", 18), 20, 35)],
    "city": [("ci_kiosk", "Kiosk", "Kiosk", kiosk("#5a6474", "#ffd890", "#ffe0a0"), 14, 30),
             ("ci_bus", "Schwebebus", "Hover Bus", rover("#c0a040", "#3a424e", "#ffe0a0", 26), 22, 40)],
    "jungle": [("ju_rover", "Geländewagen", "Jungle Rover", rover("#4a5a30", "#6a5a40", "#ffe0a0"), 20, 35),
               ("ju_tent", "Zelt", "Tent", tent("#8a7a4a", "#ffd080"), 12, 25)],
    "hive": [("hv_bulb", "Leuchtknolle", "Glow Bulb", bulb("#6a4c72", "#70f060"), 12, 25),
             ("hv_spines", "Dornenbusch", "Thorn Bush", spines("#7a5a78"), 14, 30)],
    "lattice": [("la_node", "Datenknoten", "Data Node", node_small("#4e4272", "#60e0ff"), 14, 30),
                ("la_cable", "Leitungsbündel", "Cable Run", pipes("#4e4272", "#ff4fd0"), 20, 35)],
    "corona": [("co_node", "Glutknoten", "Ember Node", node_small("#6a3c1e", "#ffb040"), 14, 30),
               ("co_cable", "Glutleitung", "Ember Conduit", pipes("#6a3c1e", "#ff8040"), 20, 35)],
    "vael": [("v6_crates", "Frachtnetz", "Cargo Net", crates("#8a96a8", "#5c6878"), 16, 30),
             ("v6_drift", "Treibkapsel", "Drift Pod", bulb("#5c6878", "#9fe8ff"), 12, 25)],
    "deep": [("v6_bulb", "Echoknolle", "Echo Bulb", bulb("#40607a", "#9fe8ff"), 12, 25),
             ("v6_spines", "Echodornen", "Echo Thorns", spines("#40607a"), 14, 30)],
}
for world in WORLDS:
    WORLDS[world] += MORE[world]
    SMALL[world] += MORE_SMALL[world]

# The ruins: what stays when a structure falls (the bullets pass over them).
model("ruin_rubble", "Ruin: rubble where a building stood.",
      ["box w=18 h=5 d=12 x=-6 z=4 rot=20 color=#3a342e noise=0.25",
       "box w=10 h=7 d=10 x=8 z=-6 rot=-30 color=#2e2a26 noise=0.25", "box w=6 h=10 d=4 x=2 z=8 rot=50 color=#4a443e",
       "greeble n=14 w=36 d=36 y=1 size=2.5 seed=21 color=#3a342e", "light r=1 x=-4 y=2 z=-2 color=#ff7020"])
model("ruin_metal", "Ruin: twisted girders and plates.",
      ["box w=2 h=2 d=28 y=2 rot=25 pitch=10 color=#3a3e44",
       "box w=2 h=2 d=22 x=6 y=2 rot=-40 pitch=-12 color=#3a3e44", "box w=14 h=1 d=10 x=-6 z=6 y=1 rot=15 pitch=20 color=#2e3238",
       "greeble n=10 w=30 d=30 y=1 size=2 seed=23 color=#30343a", "light r=0.9 x=5 y=2 z=3 color=#ff6020"])
model("ruin_crystal", "Ruin: broken crystals, still faintly lit.",
      ["crystal r=3 len=14 pitch=15 x=-5 y=2 rot=30 color=#3a2e4a emit=0.15",
       "crystal r=2.5 len=12 pitch=-10 x=6 y=2 z=4 rot=-60 color=#3a2e4a emit=0.15", "crystal r=2 len=9 pitch=5 z=-7 y=1 rot=100 color=#3a2e4a emit=0.15",
       "light r=0.8 y=2 color=#c070ff"])
model("ruin_wreck", "Ruin: wreckage drifting where it went down.",
      ["box w=12 h=2 d=20 rot=35 y=0.5 color=#2e3238 noise=0.25",
       "box w=8 h=2 d=6 x=10 z=-8 rot=-20 color=#3a3e44", "greeble n=8 w=34 d=34 size=2 seed=25 color=#2a2e34", "light r=0.8 x=-6 z=6 y=1 color=#ff6020"])
model("ruin_scorch", "Ruin: a few glowing scraps where something small stood.",
      ["box w=4 h=1.5 d=3 x=-3 z=2 rot=30 color=#2e2a26",
       "box w=3 h=1 d=4 x=3 z=-2 rot=-20 color=#3a342e", "light r=0.6 x=1 y=1 z=1 color=#ff6020"])
# A long life: they stay until the ground has carried them off the field, however slowly it goes.
for ruin in ("ruin_rubble", "ruin_metal", "ruin_crystal", "ruin_wreck", "ruin_scorch"):
    enemies += ["", "enemy " + ruin, "  model " + ruin, "  kind ground", "  scenery 1", "  invulnerable 1", "  solid 0",
                "  hp 1", "  radius %d" % (10 if ruin == "ruin_scorch" else 20), "  score 0", "  face fixed", "  life 900",
                "  scale %g" % (1.6 if ruin == "ruin_scorch" else 1.5), "  explode small"]

for world, items in WORLDS.items():
    for name, title, title_en, parts, hp, score, ruin in items:
        model(name, "%s (%s): a structure on the ground, to be shot." % (title, title_en), parts)
        enemies += ["", "enemy " + name, '  title "%s"' % title, '  title_en "%s"' % title_en, "  model " + name, "  kind ground",
                    "  scenery 1", "  ruin " + ruin, "  hp %d" % hp, "  radius %d" % (24 if hp < 100 else 30), "  score %d" % score,
                    "  face fixed", "  life 900", "  scale 1.5", "  drop credits 0.2 value=%d" % max(10, score // 4),
                    "  drop power 0.02 mode=arcade",
                    "  explode %s" % ("medium" if hp >= 100 else "small")]
    for name, title, title_en, parts, hp, score in SMALL[world]:
        model(name, "%s (%s): a small thing on the ground, to be shot." % (title, title_en), parts)
        enemies += ["", "enemy " + name, '  title "%s"' % title, '  title_en "%s"' % title_en, "  model " + name, "  kind ground",
                    "  scenery 1", "  ruin ruin_scorch", "  hp %d" % hp, "  radius 12", "  score %d" % score, "  face fixed",
                    "  life 900", "  scale 1.6", "  drop credits 0.06 value=10", "  explode small"]

with open(os.path.join(ROOT, "data", "models", "structures.txt"), "w", encoding="utf-8", newline="\n") as f:
    f.write("# The ground's destructible structures and their ruins, a set for each world. Written by tools/gen_structures.py.\n")
    f.write("\n".join(models) + "\n")
with open(os.path.join(ROOT, "data", "enemies", "structures.txt"), "w", encoding="utf-8", newline="\n") as f:
    f.write("# The ground's structures (scenery: they do not fire, the pilots' shots pass on through them, seeking weapons and the\n"
            "# kill count leave them alone; destroyed they leave a ruin and a little money that grows with the episode).\n"
            "# Which stand on which ground: the themes' \"structures\" lines. Written by tools/gen_structures.py.\n")
    f.write("\n".join(enemies) + "\n")

# Which structures stand on which ground, and how far apart (field units of ground between two groups).
THEMES = {
    "station": (95, "station"), "foundry": (85, "foundry"), "lava": (110, "foundry"), "desert": (105, "desert"),
    "ocean": (150, "ocean"), "storm": (140, "ocean"), "ice": (110, "ice"), "city": (85, "city"), "jungle": (110, "jungle"),
    "river": (120, "jungle"), "hive": (110, "hive"), "cave": (130, "desert"), "lattice": (100, "lattice"),
    "corona": (115, "corona"), "e5_corona": (115, "corona"), "e6_bands": (125, "vael"), "e6_storm": (135, "vael"),
    "e6_deep": (110, "deep"),
}
for path in (os.path.join(ROOT, "data", "themes.txt"), os.path.join(ROOT, "data", "levels", "e5", "e5_themes.txt"),
             os.path.join(ROOT, "data", "levels", "e6", "e6_themes.txt")):
    s = open(path, encoding="utf-8", newline="").read()
    s = re.sub(r"\n  structures [^\n]*", "", s)
    for theme, (every, world) in THEMES.items():
        # The buildings a little denser than the numbers above say, the small things between them twice as often.
        line = "  structures every=%d group=3 %s\n  structures every=%d group=4 %s" % (
            round(every * 0.75), " ".join(n for n, *_ in WORLDS[world]), round(every * 0.45), " ".join(n for n, *_ in SMALL[world]))
        s = re.sub(r"(\ntheme %s\n)" % re.escape(theme), r"\1" + line.replace("\\", "\\\\") + "\n", s)
    open(path, "w", encoding="utf-8", newline="").write(s)
print("structures:", sum(len(v) for v in WORLDS.values()), "buildings and", sum(len(v) for v in SMALL.values()), "small things in", len(WORLDS), "worlds:", ", ".join("%s %d" % (w, len(WORLDS[w]) + len(SMALL[w])) for w in WORLDS))
