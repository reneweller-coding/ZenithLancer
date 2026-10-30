# Zenith Lancer – data files

Everything the game is made of lives in `data/` as plain text. The program reads every `.txt` below `data/`
(except `data/story/<other language>/`). Run `ZenithLancer --validate` after every change: it lists errors
(the game would misbehave) and warnings (unknown values, missing models or texts).

## Syntax

```
# a comment (a "#" followed by a space, or at the start of a line)
enemy dart                       <- a block: its type and its name
  model dart                     <- lines of the block: a key, then words
  fire aimed every=1.2 speed=220 color=#ff6040   <- key=value words are named values
  title "Two words"              <- quotes keep words together
> A line of prose (story texts)  <- lines starting with ">" are prose
```

- A block's head line starts at the beginning of the line; the block's own lines are **indented** (two spaces).
  That is how `model e1_dart` inside an enemy is a value and not a new model block.
- Names are lower case ASCII with `_` (`e1_drone`, `boss_warden`). Every name is global per type: two episodes
  must not define the same enemy or model name, so prefix with the episode (`e3_borer`).
- Numbers may be ranges `a..b` where noted (guns: the value grows from the first to the last power level).
- Colours: `#rgb`, `#rrggbb`, or palette names: white black steel gunmetal graphite silver ivory sand rust crimson
  red orange amber gold yellow lime green olive teal cyan sky blue navy violet magenta pink bone flesh moss chitin
  plasma ember.

## The field and directions

- The field is 720 wide and 800 high (field units). x grows to the right, y grows **down** the screen.
- Enemies usually enter above the top (y = -40) and fly down. The pilot lives in the lower part.
- Angles are in degrees: **0 points down the screen**, 90 to the left, 180 up, -90 (or 270) to the right.
- Time in missions is in seconds from the start (the timeline stands still while a boss lives).
- Speeds are field units per second (the field is 800 high: 200 crosses it in 4 s).

## Models (`data/models/*.txt`)

A model is built from parts. Model space: x right, y up towards the camera, **-z is the nose** (forward). One unit
is one field unit; a small fighter is ~30 long, a pilot's ship ~36, a boss 150–300.

```
model dart scale=1
  part hull len=34 wid=12 hgt=7 shape=dart color=#a33
  part wing span=16 root=14 tip=4 sweep=8 z=4 thick=1.4 color=#822 stripe=#ddd
  part engine r=3 len=8 x=4 z=14 glow=#ff8030
  part dome rx=3 ry=2.5 rz=5 z=-4 y=3 color=#4cf emit=0.4
```

Every part takes `x y z` (its place), `rot` (degrees about the up axis), `pitch`, `roll`, `color`, `emit` (0..1:
how much it glows; 1 = full light, good for lamps, cockpits, cores), `noise` (facet brightness variation, default
0.06), `mirror=1` (also built mirrored at -x; wings and off-centre engines/guns mirror by default), and
`group=head` (turns towards the pilot when the enemy has `aimhead 1`; give `pivot=x,z`) or `group=spin` (spins,
`spin=` degrees/s).

| part | values |
|---|---|
| `hull` | `len wid hgt shape=dart/cigar/wedge/blob/box/needle/tear/hammer/arrow/hex seg=8 round=2 (higher = boxier) belly=0.55 nose= tail=` — a body lofted along z |
| `wing` | `span root tip sweep thick dihedral stripe=colour` — from x=0 outwards to +x (mirrored) |
| `fin` | `h root tip sweep thick` — a vertical plate |
| `engine` | `r len r2 glow=colour seg` — a cylinder along z with a glowing nozzle at +z (exhaust is drawn there) |
| `gun` | `r len` — a barrel along z |
| `cone`, `spike` | `r len r2` — a cone, tip forwards (-z) |
| `disc` | `r h seg` — a flat cylinder about the up axis (saucers, turret bases) |
| `dome` | `r rx ry rz glass=1` — a half ellipsoid on top |
| `sphere`/`ball` | `r rx ry rz seg` |
| `light` | `r` — a small glowing ball (always full glow) |
| `box` | `w h d taper` (taper: top smaller) |
| `ring` | `r thick seg` — a torus about the up axis |
| `crystal` | `r len seg` — a double pyramid along z |
| `plate` | `pts="x,z;x,z;..." thick` — a flat star-shaped outline |
| `greeble` | `n w d size seed` — random small boxes on an area (detail) |

Use `--gallery work/gallery.png` to see every model on one sheet.

## Enemies (`data/enemies/*.txt`)

```
enemy e1_dart
  title "Dart"                 # shown on boss bars
  model e1_dart
  kind air                     # air | ground | boss | part | obstacle
  hp 8
  radius 14                    # hit circle (field units; ground enemies are scaled automatically)
  score 60                     # points, and credits in the campaign
  speed 200                    # path speed
  path dive turn=60            # its usual path (a wave may override any value)
  face move                    # move | down | up | player | spin | fixed
  aimhead 1                    # the model's head group turns to the pilot
  scale 1                      # model size
  fire aimed every=1.6 delay=0.8 speed=220 bullet=orb color=#ff7050
  drop credits 0.3 value=40    # what  chance  [value=] [item=] [mode=campaign|arcade]
  drop power 0.08 mode=arcade
  explode small                # small | medium | large | huge
  contact 15                   # damage when it rams the pilot
  part e1_turret x=-20 z=10    # parts fixed to it (their own enemy entries, kind part); x/z scale with `scale`, y lifts it
  shielded 1                   # cannot be hurt while parts live
  solid 1 / invulnerable 1     # obstacles: bullets stop / it cannot be destroyed
```

Ground enemies stand on the ground below and move with it; all guns hit them. Bosses hold the timeline until
they die; give them parts and phases. Parts sit in the parent's frame: x to its right, z towards its nose.

HP guide (normal difficulty): popcorn 4–12, fighters 15–40, gunships 60–150, heavy ground 80–250, mini-bosses
400–1200, bosses 2500 (episode 1) to 12000 (episode 5). The pilot's front gun does ~25 dmg/s at level 1 and
~150–250 at level 11; with rear gun and sidekicks a late ship does 300–500.

### Fire patterns

`fire <pattern> key=value ...` — several per enemy. Common values: `every` (s), `delay` (first shot), `speed`,
`n`, `spread` (degrees between bullets, or units for `down`), `dir` (degrees, 0 down), `bullet`
(orb needle big ring missile mine shard wave), `color`, `burst` (volleys in a row) + `gap`, `rot` (degrees each
volley turns: rings/spirals), `accel`, `x`/`z` (muzzle in the enemy's frame), `mirror=1` (also from -x),
`cap` (max volleys), `from`/`until` (only while hp share ≤ from and > until: boss phases), `diff=N`
(only from difficulty N; `diff=A-B` only in that span), `size`, `damage`, `life`. With `mirror=1` the second muzzle
mirrors `dir` and `rot` too (symmetric beams and fans).

| pattern | what |
|---|---|
| `aimed` | n bullets fanned by `spread` at the pilot |
| `fan` | n bullets around `dir` (for `face fixed/spin` enemies relative to their heading) |
| `ring` | n bullets all round, turning by `rot` each volley |
| `spiral` | like ring, usually small n, short `every`, `rot` 10–20 |
| `down` | n bullets side by side (`spread` = spacing in units) going `dir` |
| `rain` | n bullets randomly within ±`spread` of `dir` |
| `wall` | a row across the whole field with a gap to slip through |
| `missile` | homing bullets (`turn` degrees/s, homing stops after 2.5 s) |
| `mine` | drifting mines that burst into a ring |
| `laser` | a beam: `warn` (s of warning line), `dur`, `width`, `damage` (×4 a second while it touches: 40 costs 160/s); aimed unless `dir`/`rot` given (or `aim=1`/`aim=0` says so); `rot` sweeps (degrees/s) |
| `spawn` | launches an enemy: `spawn=e2_drone` (its own path, or `path=` and path values on the fire line) |
| `radio` | says a radio line once: `fire radio e5_10_r7 from=0.5` (a boss's taunt at half strength) |

Bullet damage defaults: orb 6, needle 5, big 12, ring 8, missile 10, mine 10, shard 5, wave 7. The difficulty
scales fire rate, bullet speed and damage. Keep normal difficulty fair: telegraph big attacks, leave gaps.

## Paths

`path <name> key=value` on an enemy or `path=<name>` on a wave line. Values: `speed`, and:

| path | values | what |
|---|---|---|
| `straight` | `dir accel` | a straight line |
| `dive` | `turn` | down, leaning towards the pilot at first, accelerating |
| `sine` | `amp freq phase` | down, weaving |
| `zigzag` | `amp freq` | down in a zigzag |
| `swoop` | `side=1/-1 cx cy dur` | in from the left (1) or right (-1), a curve through (cx, cy), out the other side in `dur` s |
| `arc` | `side turn dir` | in from above, bending away to one side |
| `strafe` | `side y amp freq` | across the field at height y |
| `uturn` | `stop r` | down to `stop`, a U-turn, away up |
| `loop` | `stop r` | down to `stop`, a full loop, on down |
| `hover` | `stop wait amp freq exit` | down to `stop`, stays `wait` s swaying, leaves towards `exit` (0 down, 180 up) |
| `chase` | `turn dur dir` | follows the pilot for 2×`dur` s, then flies on |
| `orbit` | `cx cy r side dur` | circles a point for `dur` s, then down |
| `rise` | `amp freq` | from below the bottom upwards (use rarely, warn the player) |
| `drift` | `dir` (sideways drift) | slow, swaying (rocks, mines) |
| `boss` | `stop cx amp freq` | down to `stop`, then wanders across the top (faster when weak) |
| `spline` | `pts="x,y;x,y;..." dur` | through points in `dur` s (field coordinates) |
| `ground` | `dir speed` | stands on the ground; with dir/speed it drives |
| `still` | | does not move (parts) |

## Missions (`data/levels/<episode>/*.txt`)

```
level e1_01
  title "Glutpforte"
  episode 1
  theme lava                  # data/themes.txt
  music e1_a                  # assets/music/e1_a.ogg (missing files are silent)
  scroll 60                   # the ground's speed
  length 200                  # seconds (the mission ends after the last event and an empty field)
  next e1_02 e1_03            # the missions that may follow in the campaign (a choice when several)
  secret e1_s1 key=e1_key     # a secret mission, open when the key was picked up here
  issecret 1                  # this one is secret (map ring)
  arcade 1                    # it belongs to the arcade run (order in data/episodes/eN.txt)
  map 0.2 0.8                 # where it lies on the episode map (0..1, y down)
  briefing e1_01_brief        # story entry shown before
  debrief e1_01_after         # ... and after (optional)

  at 3   text e1_01_r1                                    # a radio line (story entry of type radio)
  at 4   wave e1_dart n=5 form=line x=360 spacing=70 path=dive
  at 9   wave e1_dart n=6 form=column x=120 gap=0.35 path=swoop side=1 cx=360 cy=300 dur=4 mirror=1
  at 20  ground e1_turret x=180
  at 20  ground e1_turret x=540
  at 30  pickup cube x=300 item=e1_c01                    # a log crystal (story entry of type cube)
  at 40  wave e1_drone n=8 form=random range=500 gap=0.4 diff=3     # only from difficulty 3 (Schwer) on
  at 50  wave e1_dart n=4 form=v x=360 repeat=3 every=6   # three times, 6 s apart
  at 170 warn
  at 173 boss e1_boss_warden
  at 175 end                                              # the end follows ~3 s after (boss dead first)
```

Events: `wave <enemy>` / `ground <enemy>` / `obstacle <enemy>` (all take the wave values below), `boss <enemy>
[x= y= path values]`, `pickup <credits|power|shield|armor|weapon|rear|side|life|special|cube|key|plan> x= [y=]
[value=] [item=]`, `text <story>`, `scroll <speed>`, `music <name>`, `warn` (siren + WARNUNG), `clear` (removes
enemy bullets), `end [seconds]`.

Events after a `boss` wait until it is destroyed (the timeline stands); give them `hold=0` to let them run on
the clock during the fight (radio lines, help pickups): `at 150 text e1_01_r5 hold=0`.

`theme <name>` changes the ground behind a flash of light (out of the clouds into orbit, into a cave).
Fire lines may wait for the parts: `parts=0` fires only when all parts are destroyed, `parts=1` only while one lives.

Every event line may have `diff=N` (from difficulty N on) or `diff=A-B`, `repeat=N every=S`.

Wave values: `n`, `form` (single column line v arc random sides grid), `x` `y` (start; y defaults -40),
`spacing` (line/v/arc/grid), `gap` (seconds between the ships of column/random/sides), `range` (random: width),
`cols` `dy` (grid), `mirror=1` (a mirrored copy: x → 720 - x, side flipped), `delay`, `hp` (multiplier),
`drop=<what> [item=] [value=]` (left by the wave's last ship when all of it is shot down), and any path value
(`path speed amp freq stop wait dir turn cx cy r side dur pts exit accel phase`).

Pacing guide: missions of 150–240 s; something on the field all the time; a breath every 30–40 s; a mid-boss or
a set piece at the middle of longer missions; a boss at the end of the episode's key missions. 2–4 log crystals
and ~6 credit pickups per mission; plans (`pickup plan item=<good>`) and keys hidden in harder spots (reward
killing a whole wave: `drop=plan item=...`).

## Themes (`data/themes.txt`)

```
theme lava
  sky #120604               # the colour behind everything
  fog #401008 0.0015        # colour and density with depth below the field
  sun -0.4 -0.8 -0.3        # light direction
  suncolor #ffd2a0 1.3      # colour, strength
  ambient #6a4a40 #201010   # from above, from below
  relief 80                 # height of the ground's relief
  scale 0.004               # how fine the relief is
  ridge 0.5                 # 0 soft hills .. 1 sharp ridges
  ground #2a1a14 #4a3020 #6a4a34 #9a8070   # low, mid, high, peak colours
  water -0.3 #ff5010 0.9    # a level (in relief units), its colour, its glow (lava 0.6–1, water 0)
  glow #ff6020 0.5          # glowing veins
  craters 0.2               # 0..1
  city 0.3                  # buildings 0..1
  trees 0.5 #2a6030         # forest density, colour
  plates 1                  # station plating
  props #707880             # colour of buildings and hardware
  clouds #ffffff 0.5        # cloud layers between ground and field
  space 1                   # no ground: stars (with rocks 0..1 floating rocks, nebula #a #b)
  exposure 1  bloom 0.9
```

## Workshop goods (`data/items/*.txt`)

```
front pulse
  title "Pulslanze"
  desc "..."
  price 600                 # upgrades cost more each level (Data.cpp upgradePrice)
  tier 1                    # the episode whose workshop offers it first
  hidden 1                  # only after its plan was picked up
  rate 6..9                 # volleys a second, from level 1 to 11
  energy 4..9               # generator energy a volley
  sound pulse               # pulse laser missile plasma flame beam spread rail chain
  shot row lv=1-11 n=1..3 dx=10 speed=900 dmg=5..9 size=4 color=#7cf sprite=pulse
  shot fan lv=6-11 n=2..4 spread=20..40 dmg=3..5 every=2
```

Each `shot` line fires at levels `lv=a-b`; numbers may be ranges over that span. Kinds: `row` (n parallel,
`dx` apart), `fan` (n over `spread` degrees), `side` (n pairs at ±`angle`), `back`, `homing` (`home` turn rate),
`wave` (`amp freq`), `burst` (random within `spread`), `flame`, `bomb` (`radius` blast), `bounce` (`bounce`
walls), `orbit` (circles the ship: `range spin life`), `beam` (a column: `width`, all in it take `dmg` each volley;
`pierce=1` goes through everything), `chain` (lightning: `range chain`). Common: `angle` (0 = straight ahead),
`speed dmg size life pierce accel drag x y every split length color sprite`, `jitter` (degrees of random scatter),
`slow` (slows to a stop: flames), `phase` (degrees: where a weaving shot starts its swing; a negative `amp` swings the
other way), `split` (bursts into n weaker splinters on impact or at the end of its life), `angle` (also for homing
fans). Beams fire as often as `rate` says (60 for a steady beam) and burn where their shot line's `x`/`y` puts them
(sidekicks' beams from the sidekick). Sprites: pulse bolt laser plasma rocket flame spark star ripple bomb rail petal
needle_p orbit.

Rear guns: `rear <name>` with `mode1 "Name"` and `mode2 "Name"`; shot lines take `mode=1` or `mode=2`.

Sidekicks: `sidekick <name>` with `model`, `place` (fixed orbit trail front), `aim` (forward nearest), `rate`,
`ammo` + `reload` (a shot comes back every reload s), `charge` (s until it fires once), `shot ...` lines (level 1).

Hulls: `ship <name>` with `model armor speed accel radius gen` (gen: generator bonus 0.1 = +10 %).
Generators: `power` (energy/s) `capacity`. Shields: `max regen cost delay`. Specials: `kind` (nova barrier drones
timeslow repair magnet lance shield missiles mines) `charges` and values (`radius power time color count`): shield
fills shield and generator at once (`time` without harm), missiles fires `count` homing missiles of `power`, mines
lays a ring of `count` mines (`power`, `radius`, `time` until they burst).

## Episodes (`data/episodes/eN.txt`), arcade pilots (`data/arcade.txt`), difficulties (`data/difficulty.txt`)

```
episode 1
  title "Glutgürtel"
  subtitle "..."
  first e1_01
  intro e1_intro            # story entries
  ending e1_ending
  credits 3000              # start money when begun fresh
  ship lancer
  front pulse 1
  rear tailgun 1            # or leave out
  side pip -                # left right ("-" none)
  gen cell
  shield veil
  special nova
  arcade e1_01 e1_03 e1_05 e1_07 e1_09 e1_10
```

## Stories (`data/story/de/*.txt`)

```
cube e1_c01
  title "Wartungsprotokoll 7"
  from "Gießerei Ascalon"
> First paragraph ...
>
> Second paragraph (an empty ">" line separates paragraphs).

radio e1_01_r1
  from "ORRIN"
> One or two short sentences (they show for 6 s at the bottom of the field).

briefing e1_01_brief
  title "Glutpforte"
  from "Bastian Quill"
> ...
```

Types: `cube` (log crystals: lore, 80–200 words), `briefing`, `debrief`, `radio` (≤ 25 words), `intro`, `ending`, `bio`.
A wave line may give its ships another facing: `face=down` etc.
