# Mods for Zenith Lancer

Everything the game knows – guns, ships, sidekicks, enemies, their fire patterns, missions, episodes, stories,
difficulties, arcade pilots, ground themes – is plain text under `data/`. A mod is a folder with more of that text.
The game reads its own data first and each mod switched on over it: an entry with a name the game already has
replaces the game's, a new name adds to it.

## Where mods live

- `<user folder>/mods/<name>/` – Windows `%APPDATA%\ZenithLancer\mods`, Linux and the Steam Deck
  `~/.local/share/ZenithLancer/mods`
- `mods/<name>/` beside the game (the release brings `mods/example`)

A mod folder holds a `mod.txt` and a `data/` folder:

```
mods/sparkler/
  mod.txt
  data/
    items/sparkler.txt
    pilots.txt
```

`mod.txt` is a few `key value` lines: `title`, `title_en`, `author`, `desc`, `desc_en`.

In the game: main menu → **Mods** → switch it on (Enter / A). Leaving the screen reads the data again.

## What a mod can do

- **Add** anything: a gun (`front` / `rear` in any file under `data/`), a hull, a sidekick, a generator, a shield, a
  special, an enemy and its model, a mission, an arcade pilot, a difficulty, a ground theme.
- **Replace** anything by its name: a `front pulse` block in a mod is the Pulse Lance from then on.
- A mod's enemies (with a `title`) show in the archive once they have been met. Replays keep a checksum of the data
  they were flown with; one recorded with other mods is marked as possibly drifting.
- **Stories**: German under `data/story/de/`, English under `data/story/en/` (same names as the German ones).
- **A whole episode**: `data/episodes/e6.txt` with its missions under `data/levels/e6/`. It opens in the campaign after
  episode 5 has been finished.

The syntax of every file is in [DATA.md](DATA.md); the game's own `data/` is the best example. Interface texts
(`data/lang/`), the music and the pictures are not part of a mod: a mod's missions choose among the game's music
(`assets/music`) and themes.

## Checking a mod

```
ZenithLancer --validate --mod mods/sparkler
```

reads the game's data and the mod's over it and lists every error with its file and line. A mod with errors still
loads; the Mods screen shows how many there are.

## The example

`docs/example_mod` (in the release `mods/example`) adds Quill's **Spark Spitter**, a front gun of tier 1, and the
arcade pilot **Sparrow** who flies it. Copy it, rename it, change it.
