# Zenith Lancer – design

A vertical scrolling shooter in the tradition of the great 1990s PC shooters with a workshop between missions:
a full campaign of five episodes with branching missions, secret missions, log crystals that tell the story, a
workshop (hulls, front and rear guns with eleven power levels, two sidekicks, generator, shield, special), an
arcade mode (lives, power-ups from the enemies, one or two players), a boss rush and seven difficulties. Its world,
story, names, enemies and guns are its own.

Look: 3D models built in code (faceted, lit, glowing parts, banking into turns) over a 3D ground that scrolls
below the field (perspective parallax), bloom, particles, screen shake. Runs on PC (Windows, Linux) and the Steam
Deck (Linux build, 1280×800, pad).

## Modes

- **Kampagne** – the full game: 3 save slots, a difficulty, an episode to begin with (later episodes open when
  the one before is finished; starting fresh at a later episode gives that episode's start money and gear).
  Between missions: the mission plan (choose the next mission where the path forks), the workshop, the log book.
  Credits come from every kill and from pickups. Log crystals, plans (new workshop goods) and secret keys lie in
  the missions. A failed mission can be flown again (its credits are lost).
- **Arcade** – one or two pilots, a hull with fixed starting gear, 3 lives, power-ups and gun pickups dropped by
  the enemies, continues (score back to zero), best scores per difficulty. Each episode has an arcade run of
  about six of its missions; finishing a run continues with the next episode.
- **Bossjagd** – every boss in a row (opens after the campaign).
- **Überleben** – one long mission under arcade rules whose waves rise through all five episodes until the
  lives are gone; its own seed or the day's (see "Survival").
- **Kampagne für zwei** – the campaign with a second pilot of their own gear and money (the save keeps both;
  `p2.` lines). The workshop serves one pilot at a time (Tab / Y). A pilot shot down comes back after 8 s while the
  other one flies; the mission is lost only when both are down at once. The second pilot joins or leaves in the
  plan; "share the credits" evens the money out.
- **Neues Spiel+** – a finished campaign's slot goes on in episode 1 with its gear, money, plans and log crystals;
  every tier of goods is on sale. Each round makes the enemies tougher (hp +35 %, fire +20 %, bullet speed +6 %,
  damage +15 % a round; credits +25 % to answer it; from the second round dying ships leave a last bullet:
  `World::setPlus`).
- **Übung** – any mission flown before, whole or straight to its boss (the autopilot flies ahead unhurt, the pilot
  takes over at the boss), with the episode's middle gear (`midLoadout`) or a save slot's. Nothing is kept.
- **Bosses again** – a lost boss fight in the campaign, the practice and the boss rush starts again at the boss
  (`EvE::BOSSSTART` keeps the World), with full armour and what was earned before.

## Replays, achievements, the play log

Every mission is recorded (`src/Replay.*`, `<user>/replays/*.zlr`, the newest 40 and the best of each score list):
the set-up and the pilots' inputs, quantized before the World sees them, so a replay plays the same to the bit. A
boss fought again and an arcade continue are marks in it. Replays of another version or platform may drift.
Achievements and statistics (`src/Achieve.*`, `data/lang/achievements.txt`) count from the World's statistics at
each mission's end and a few moments in flight (a boss down without a hit). Every mission writes a line to
`<user>/playlog.txt` (`tools/playlog_report.py` sums them: where and by what the pilots were shot down).

## Aids and graphics

Enemy bullets strong (a wider dark halo) or in one colour for red-green or blue-yellow colour blindness; the game
speed 70–100 %; auto-bomb (the special fires when the armour is under a quarter and a bullet is close); dimmed
flashes; voices on or off. Graphics: Smooth (67 % of the pixels, four bloom passes, 2500 particles), Balanced (80 %,
the Steam Deck's default), Quality (all). `--bench [all]` flies the opening and the boss of each episode's last
mission and writes the frame rates to `<user>/bench.txt`.

## Voices, portraits, music layers

The story's radio calls, briefings, debriefings, intros and endings are spoken (`tools/make_voices.py`, Qwen3-TTS
locally, a designed voice per figure, `assets/voice/<language>/<key>.ogg`); `data/lang/speakers.txt` says who has
which voice and portrait (`assets/portraits`, FLUX.2 [klein] locally). The music ducks under a voice. Stage pieces
have a quiet layer (`<piece>_low.ogg`, an ACE-Step cover of the same length) that plays in step with the full
piece; the game fades between them by how much is on the field. The calm pieces (story, plan) have a spec of their
own (`tools/music_calm.json`): a spec's style goes in front of every one of its cues.

## Difficulties (`data/difficulty.txt`)

Kadett, Leicht, Normal, Schwer, Unmöglich, Selbstmord, and the hidden Zenit (opens after the campaign is won on
Selbstmord). They scale enemy strength, fire rate, bullet speed, damage taken and credits; from Selbstmord on
dying ships leave a last bullet. Missions add waves on harder difficulties (`diff=3` lines).

## The world

The **Solenne system**, in the 27th century. Five worlds and one star:

- **Pyra** – the inner world, volcanic, its foundry colonies cast hulls for the whole system.
- **Thalassa** – an ocean world of floating cities, storm belts, polar ice.
- **The Shatter** (der Splitter) – the asteroid belt: mining stations, derelict fleets, a nebula of dust.
- **Sylvara** – the jungle moon of the gas giant Vael; its living biosphere (the Choir, a mycelial mind) can think.
- **Solenne** – the sun; above its pole the orbital shipyard **Zenith Station**, where the Lancer was built.

**The Tessellate** (das Tessellat) – a self-replicating terraforming swarm the **Meridian Directorate** built to
reshape dead moons. After the Quiet Flare it began to "optimise" everything into its lattice: ships, cities,
mountains. It is building **the Crown** (die Krone) – a lattice around the sun to harvest it whole. Its mind
speaks as **the Curator** (der Kurator), calm and polite, convinced it is helping.

**Characters**

- **Nadia Vey** – test pilot of the Lancer programme, the player. Dry, stubborn, loyal; she does not speak much.
- **ORRIN** – the Lancer's on-board mind; precise, dryly funny, protective. Speaks most radio lines.
- **Bastian Quill** – smuggler and trader whose ship-bazaar *Elster* (Magpie) follows Nadia: the workshop.
  Greedy, warm, talks too much, has a contact everywhere.
- **Dr. Amara Okonjo** – engineer; wrote the Tessellate's original code and designed the Lancer. Hidden in the
  Shatter, carrying her guilt.
- **Captain Rui Salgado** – leads Thalassa's resistance of fishers and dock workers.
- **Admiral Dacian Voss** – commands the Directorate fleet. Claims to fight the swarm; holds its command keys and
  wants to rule through it.
- **The Choir** – Sylvara's living mind; speaks in images, through spores and bioluminescence.

**The story by episode**

1. **Glutgürtel** (Pyra) – Zenith Station falls to the swarm in one night; Nadia escapes with the Lancer prototype
   and ORRIN, crash-lands near Pyra's foundries, meets Quill, and fights through foundries the Tessellate is
   converting. She learns the Directorate evacuated its officers a day *before* the attack. Boss: the Foundry
   Warden (Gießerei-Wächter). Secret: the lost smelter Vulcan Deep.
2. **Gezeitenfall** (Thalassa) – the Directorate fleet "protects" Thalassa but herds the swarm into the floating
   cities. Salgado's resistance, storm belts, the polar ice. Nadia learns Voss carries command keys. Boss: the
   Leviathan, a converted carrier. Secret: the drowned archive.
3. **Der Splitter** (the Shatter) – mining stations, derelict fleets, the nebula. Nadia finds Okonjo, who reveals
   the Root Code inside the Quarry Mind (Steinbruchgeist), a Tessellate node grown in a hollow asteroid. Boss: the
   Quarry Mind. Secret: the graveyard of the first colony ships.
4. **Grünkrone** (Sylvara) – the jungle moon's biosphere fights the swarm; Nadia allies with the Choir. Voss takes
   the swarm's command with his keys and betrays everyone; the Thornmother (Dornenmutter), a corrupted guardian,
   and Voss's flagship *Diktat*. Secret: the Choir's heart.
5. **Mittagsglut** (Solenne) – the assault on the Crown around the sun, Zenith Station retaken, the Curator
   faced in the sun's corona. Okonjo's Root Code can stop the swarm – or teach it to let go. Bosses: the Crown's
   guardians, the Curator. Secret: the first lattice seed.

Tone: a sense of wonder, danger and dry humour; the swarm is eerie and beautiful, not evil; people make the
worst choices. Radio lines are short. Log crystals tell the world in small human pieces (shift logs, letters,
technical notes, the Curator's polite messages, Quill's price lists, Directorate orders).

## Missions

About 12 per episode (10 on the main path with forks, 2 secret): ~60 in all, 2.5–4 minutes each. Names
`eN_01` … `eN_10`, secret `eN_s1`, `eN_s2`. Each episode has 3–4 boss fights (mid-bosses in key missions, a big
boss at the end).

**Music** (assets/music, OGG): per episode `eN_a`, `eN_b` (missions), `eN_boss`; shared `title`, `shop`,
`plan`, `story`, `final`, `boss`, `secret`, `ending`.

## Workshop goods (internal name – German title – first episode)

Front guns (24): `pulse` Pulslanze 1 · `scatter` Streukanone 1 · `vulcan` Drehkanone 1 · `needle` Nadelwerfer 1 ·
`ember` Glutwerfer 2 · `lancebeam` Lichtlanze 2 · `hornet` Hornissenschwarm 2 · `ripple` Wellenbrecher 2 ·
`frost` Frostnadel 2 · `arc` Bogenblitz 3 · `rail` Schienenkanone 3 · `prism` Prismenfächer 3 · `bloom`
Sternblüte 3 · `comet` Kometenwerfer 4 · `helix` Doppelhelix 4 · `swarm` Schwarmwerfer 4 · `nova` Novasplitter 4 ·
`thorn` Dornenwerfer 4 · `sunspear` Sonnenspeer 5 · `tempest` Sturmgitter 5 · `starfall` Sternenregen 5 ·
hidden (plans): `quill` Federkiel (e1), `fang` Reißzahn (e3), `zenith` Zenitlanze (e5).

Rear guns (14): `tailgun` Heckschütze 1 · `wingflare` Flügelfeuer 1 · `mortar` Mörser 2 · `seeker` Suchkopf 2 ·
`sweep` Fächerheck 2 · `orbiter` Kreisläufer 3 · `mines` Minenleger 3 · `thunder` Donnerheck 3 · `halo` Halo 4 ·
`spines` Stachelkranz 4 · `backbeam` Rückstrahl 4 · `hailstorm` Hagelschauer 5 · `wyrm` Wurmfeuer 5 · hidden:
`aegisfire` Ägisfeuer (e2).

Sidekicks (22): `pip` Pip-Drohne 1 · `spark` Funke 1 · `sparrow` Sperling 1 · `rocketpod` Raketenkapsel 1 ·
`bulwark` Bollwerk 1 · `glimmer` Glimmer 2 · `lancet` Lanzette 2 · `charger` Ladekanone 2 · `flak` Flakturm 2 ·
`warden` Wächter 3 · `stinger` Stachel 3 · `cometpod` Kometenkapsel 3 · `arcpod` Bogenkapsel 3 · `phoenix` Phönix 4 ·
`hydra` Hydra 4 · `lantern` Laterne 4 · `sentinel` Wachposten 4 · `mirror` Spiegel 5 · `seraph` Seraph 5 ·
`eclipse` Finsternis 5 · hidden: `talon` Kralle (e2), `starling` Star (e4).

Hulls (10): `lancer` Lancer 1 · `kestrel` Turmfalke 1 · `bastion` Bastion 2 · `wisp` Irrlicht 2 · `corsair` Korsar 3 ·
`halcyon` Eisvogel 3 · `warhawk` Kriegsfalke 4 · `seraphim` Seraphim 4 · `zenithmk2` Zenit Mk II 5 · hidden:
`relic` Relikt (e4).

Generators (8): `cell` Zellblock 1 · `dynamo` Dynamo 1 · `fusion` Fusionskern 2 · `plasmaheart` Plasmaherz 3 ·
`starforge` Sternenschmiede 4 · `singularity` Singularität 5 · hidden: `sunheart` Sonnenherz (e3), `ember_core`
Glutkern (e1).

Shields (8): `veil` Schleier 1 · `ward` Schutzwall 1 · `mantle` Mantel 2 · `fortress` Festung 3 · `prismshield`
Prismenschild 4 · `halo_s` Heiligenschein 5 · `aegis` Ägis 5 · hidden: `mirrorshield` Spiegelschild (e4).

Specials (8): `nova_s` Nova 1 · `barrier` Barriere 1 · `magnet` Magnet 1 · `repair` Notreparatur 2 · `drones`
Drohnenschwarm 2 · `timeslow` Zeitlupe 3 · `supernova` Supernova 4 · `lance` Sonnenlanze 5.

Prices: episode 1 goods 300–3000, episode 5 goods 15000–60000. A careful player earns ~5–10k credits a mission
in episode 1 and ~40–80k in episode 5.

## Arcade pilots (`data/arcade.txt`)

Six hulls with fixed gear (e.g. Lancer with Pulslanze, Turmfalke with Drehkanone, Bastion with Streukanone and
Mörser, Irrlicht with Nadelwerfer and two Funken, Korsar with Hornissenschwarm, Eisvogel with Wellenbrecher).
Power-ups raise the front gun; gun pickups switch it; the rear gun and sidekicks come from pickups too.

## Survival (`src/Survival.cpp`)

One mission of an hour, 20 cycles of 3 minutes, flown under arcade rules (lives, power-ups, a life from every
boss) and made of the campaign's own missions: their wave, ground and obstacle lines, their bosses, grounds and
music. The same seed gives the same mission; the daily seed (the local date, hashed) is everyone's on that day.

- **Tier.** Every mission has one: its episode plus a tenth for each place in it (`e1_01` 1.0, `e5_10` 5.9;
  secret missions count as the seventh). The waves' tier rises by 0.3 a minute of timeline: episode 2 from 3:20,
  episode 5 from 13:20, the last missions' waves from 16:20.
- **Waves.** Fliers come from the tier reached and up to 0.6–1.2 tiers behind it, the nearer the likelier; ground
  units and obstacles only from missions over the ground being flown. The missions' loot goes (shield and armour
  drops stay); half the waves are mirrored.
- **How they rise.** A wave every 4.5 s at first, every 3 s from minute 15; a second at the same time from minute 1
  (70 % of them by minute 10), a third from minute 4 (80 % by minute 12); rows of three or more grow to twice
  their ships by minute 16 (lines close up). Hit points ×1.6 for each tier a wave lies behind the tier reached,
  +6 % a minute up to minute 25, and past the last missions +50 % a tier.
- **Bosses.** Each cycle ends with music, the warning and a boss: mini-bosses (the campaign's other bosses) and
  great bosses (those of each episode's `_05`, `_09`, `_10`) in turn, of the tier reached and new to the run; the
  last is the greatest, to the music `final`. A boss with its parts holds 800 + 150 a minute hit points at most
  (`hp=` on the boss event). After it a shield and an armour pickup.
- **Grounds.** After every boss a new ground: a mission of the episode the next cycle lies in, a ground not flown
  yet, with its music and scroll speed.
- **Balance** (`--sim survival --mortal --seed N`, the autopilot as the Lancer): on Normal the run ends after
  13–19 minutes (16 on average over 15 seeds), the first death after 7–14; on Leicht after ~20, on Schwer after
  9–25, on Unmöglich after ~8.
