# Changelog

What changed in Zenith Lancer, newest first. `Deploy/publish_release.ps1` takes a version's section as the text of
its GitHub release.

## [1.1.0] - 2026-11-13

A foreground over the field: clouds, girders, rocks and weather passing faster than the ground.

- **A foreground over the field:** clouds, smoke, mist, girders, rocks, dust, rain and snow pass between the camera
  and the field, each layer at its own height and so at its own pace – up to two and a half times as fast as the
  ground, as in the shooters of the nineties. Every world has its own (girders over the stations, rain under storm
  clouds, ash over lava, snow on ice worlds). Bullets always stay on top, the pieces thin out over the pilots' ships,
  and Options > Foreground makes it weaker or turns it off.

## [1.0.1] - 2026-11-02

A small update for the eyes.

- **Pickups look like things, not like shots:** money is a spinning gold coin, log crystals are data chips, blueprints
  sheets, keys keys, and equipment a hexagonal token with its sign (bolt, ring, plus, arrows, star). Every pickup is
  solid with a dark edge and a shadow, all but money sit in four corner brackets, and a glint passes over them now and
  then; the enemies' shots stay pure light.

## [1.0.0] - 2026-10-30

The first public release: a vertical scrolling shooter with a workshop between the missions, for Windows, Linux and
the Steam Deck, free software under the AGPL-3.0.

- **The campaign:** five episodes and 60 missions across the Solenne system, with forks in the path, secret missions
  behind hidden beacons, log crystals that tell the story, blueprints for rare gear and bosses in several parts.
- **The workshop:** hulls, front and rear guns with eleven power levels, two sidekicks, generators, shields and
  specials.
- **Arcade** for one or two pilots, the **boss rush**, **seven difficulties**, **German and English**.

### New in this release

- The campaign for two pilots.
- New Game+ for a finished campaign.
- Practice: any boss or mission you have reached, on its own.
- A daily survival mode.
- Replays, achievements and statistics.
- Accessibility: bullet contrast and colour-blind palettes, the game speed, auto-bomb, reduced flashes.
- A graphics quality setting and a benchmark mode for the Steam Deck.
- A play log.
- Mods through a `mods` folder beside the game, with an example (`mods/example`).
- Speaker portraits and voiced radio calls.
- Dynamic music layers.
- Chains and grazes: kills in quick succession raise a score multiplier up to five times, bullets that pass close
  score and keep the chain; new score lists.
- The weekly flight: a week's waves with two house rules; daily and weekly flights fly against the ghost of the best
  flight.
- The flight school: nine short lessons.
- The archive: every enemy met as a turning model, the music room, paints for both pilots.
- Videos of replays or missions (`--video`), a Steam Deck check of every screen's text size (`--deckcheck`).
- Replays carry a checksum of the data and warn when it differs.
- **Episode 6, "Afterglow"**: an epilogue after the credits over the gas giant Vael – twelve missions, fifteen new
  enemies and their models, three bosses (Ark Mercy, the Stormweaver, the Echo), four new grounds, 100 story texts
  in German and English, voiced, and four pieces of music.
- Medals for every mission, challenge codes for friends, the boss rush for two, the arcade ghost, the photo mode,
  the wave editor, a battery profile for the Steam Deck, the logbook's portraits and voices in the list.
- Golden replays as a test (`--goldentest`), a sanitizer job in CI; test runs and ffmpeg stay in the background.
- A ground full of things to shoot: every world has 26–30 kinds of its own, 333 in all, buildings (smokestacks,
  cooling towers, rigs, piers, domes, tower blocks, wind rotors, brood towers, crystal gates, aerostats ...), the small
  things between them (drums, crates, lamps, carts, boats), guns that shoot back and launch pads from which fliers
  rise into the fight. They leave ruins and debris, give points and a little money, and never get in the way of a
  shot meant for an enemy.
- Swarms: four new swarm fliers for every episode come in squads between the waves, so a mission brings 300–460
  fliers (Tyrian's planet levels: 300–550).

### Changed since 0.9.0

- English throughout: a language choice in the options (the first start follows the system's language), the
  interface, the workshop, the difficulties and the pilots, all 684 story texts and every mission, enemy and episode
  name; a glossary keeps the names the same everywhere.
- After a lost boss fight only the boss is fought again (the boss checkpoint).
- The font covers the Latin Extended-A block (names such as Ilić).
- The music check with CLAP; the calm pieces have their own prompts.
- An app test for the boss rush.
- The higher difficulties grow into their strength over the first two episodes (Impossible: the autopilot wins 58–59
  of 60 missions instead of 54).
- Three pieces of music made again (e2_b, e4_b, secret), chosen by CLAP; `tools/pick_takes.py`.
- The asset scripts find the local generators through `tools/paths.py` (no machine paths in the repository).

### Release

- Licence AGPL-3.0 (`LICENSE`); `docs/CREDITS.txt` names the tools behind the music, voices and portraits and every
  library raylib carries.
- The README for GitHub, the store texts (`docs/STORE.md`) and the release checklist (`docs/RELEASE.md`).
- GitHub workflows: `build.yml` builds and runs the headless tests for pushes and pull requests; `release.yml` builds
  both archives for a tag `v*` and attaches them to a draft release.
- `Deploy/publish_release.ps1` checks and publishes a release. The packing scripts run the headless tests first and
  let the packed program check its data; the archives carry `LICENSE` and the example mod.

## [0.9.0] - 2026-10-30

The whole game, played through and balanced.

- **Five episodes of twelve missions:** Glutgürtel (Pyra: 8 bosses, 28 enemies), Gezeitenfall (Thalassa: 6 bosses,
  28 enemies), Der Splitter (the Shatter: 28 enemies, log crystals and radio calls), Grünkrone (Sylvara: 4 bosses,
  34 enemies) and Mittagsglut (Solenne: 5 bosses with the Curator, the ending), each with its models and story.
- **The workshop:** all 94 goods with their own models, balanced; six arcade pilots. The economy of each episode is
  scaled to target values (`tools/scale_credits.py`); episodes 2 to 5 have their own starting gear.
- **The game:** splitting shots, wave phases, spread, braking shots, homing fans with an angle, several beams for a
  pilot, the specials shield charge, missile salvo and mine ring; lasers that aim, mirrored directions, paths for
  launched enemies, a life time for each enemy, a wave bonus; homing weapons avoid shielded bodies.
- **Bosses:** hits strike the boss's parts first, fire by parts, the ground's theme changes within a mission, a radio
  call as a boss phase, a softer flash under constant fire, debriefings after the missions.
- **The boss rush:** the fourteen great bosses in episode order over their own ground, with full gear; in the
  simulation as well (`--sim bossrush`). Blueprints are checked against the workshop.
- **Graphics:** a smooth ground with its own shader for each theme, animated water and lava, shadows, dark rims round
  enemy bullets, metal highlights, the new themes river and cave; hulls no longer bloom out, the hit flash flickers,
  beams leave every muzzle.
- **Music:** 23 pieces made locally with ACE-Step, two takes each, smoothed loop seams; music for each screen, the
  boss music at the warning (`tools/make_music.py`).
- Rumble on hits, explosions and bosses (it can be switched off); the program's icon (`tools/make_icon.py`).
- App tests that play scripts through every screen (campaign, arcade for two, pause, options) in ctest; a loudness
  measurement of the sound effects; abandoned arcade games go into the best scores.
- Pictures of single screens (`--screen`), of missions in any theme (`--theme`), the balancing tools `--weapons` and
  `--loadout`, `--help`; the autopilot counts crystals, keys and blueprints and flies closer with short guns.
- The Linux build (GCC 12, glibc 2.35, the Steam Deck), the release scripts and the player's readme.

## [0.1.0] - 2026-10-30

The core: the simulation, the model forge, the renderer, the ground, the sound, input, menus, the workshop, the
campaign, the arcade mode and the first mission.
