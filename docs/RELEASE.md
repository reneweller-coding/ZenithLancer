# Zenith Lancer – releasing

The steps from a finished round of work to a published release, in order. `x.y.z` is the version, `vx.y.z` its tag.
The app tests, the playing and the pictures use the graphics card: check that no other job holds it first.

## 1. The work is in

- Every branch of the round is merged into master; `git worktree list` shows no worktree with work still pending.
- `CHANGELOG.md` ("New in this release"), `README.md` ("New in 1.0.0" in both languages), `docs/STORE.md` and
  `docs/README-player.txt` describe the new modes and options as the game has them: their names in the menus, their
  keys, nothing that did not land.

## 2. The version

- `CMakeLists.txt`, `project( ZenithLancer VERSION x.y.z ... )`, is the one source: the game shows it, the scripts and
  the workflows read it. 1.0.0 is set.
- For a later release: raise it, put a section `## [x.y.z] - yyyy-mm-dd` on top of `CHANGELOG.md` (its text becomes
  the release's text), and change the download links in `README.md` (`publish_release.ps1` refuses links to another
  version).

## 3. Tests

- `.\build.ps1 -Test`: all tests, the app tests included (campaign, arcade for two, boss rush, played by scripts in a
  hidden window, with their own test folders).
- `bin\msvc\ZenithLancer.exe --validate`: `OK: 0 errors, 0 warnings`.
- `bin\msvc\ZenithLancer.exe --langcheck all`: no text without its English.
- For the balance, where the round changed it: `--simall --difficulty N --mortal` for each episode.

## 4. Playing and listening

- **Windows:** one mission in each episode, a boss fight lost once (only the boss again), the workshop (buy, sell, the
  generator's limit), the arcade for two with two pads, the boss rush, a secret mission.
- **New in 1.0.0:** the campaign for two with two pads; New Game+ from a finished save; practice of a boss and of a
  mission; daily survival (and a new run after midnight); a replay recorded and played back; an achievement unlocked
  and the statistics counting; each accessibility option (bullet contrast, every colour-blind palette, game speed,
  auto-bomb, reduced flashes); each graphics quality and the benchmark; the play log written; the example mod from
  `mods/example` loaded, and a broken mod reported instead of a crash; the flight school to its end; the weekly
  flight with its rules and, flown twice, the ghost; chains and grazes in the HUD; the archive's three pages (a model
  of an enemy met, a piece played in the music room, a paint on the ship in a mission); episode 6 after the credits
  in the same slot (its intro on the slot's next start) and New Game+ after its end; a medal in the plan; a
  challenge code typed in and pasted; the boss rush for two; the photo mode's picture in shots; the wave editor's
  test flight and the saved mission in practice after a restart; the battery profile on the Deck.
- **Listening:** every piece of music once (title, shop, plan, story, each episode's missions and boss, secret, final,
  ending) against the effects; the music layers rising and falling without clicks; the voiced radio calls against
  music and effects, the right speaker and portrait, nothing clipped or garbled, in German and in English.
- **Languages:** the new screens once in German and once in English (Options > Language).
- **Steam Deck:** the Linux archive on the Deck, added as a non-Steam game, started in Gaming Mode: full screen at
  1280 x 800, the Deck's controls, each graphics quality, the benchmark's numbers (for `docs/STORE.md`), suspend and
  resume. `ZenithLancer --deckcheck` draws every screen at 1280 x 800 with the Deck's text size and names the smallest
  text (30.10.2026: 14.9 px, the HUD's labels; Valve asks for at least 9 px).
- **Trailer:** `python tools/gpu_queue.py python tools/make_trailer.py` cuts about a minute from missions flown by the
  autopilot (cards in the game's font, the workshop, the title music) into `dist/trailer/zenith_lancer_trailer.mp4`
  (attach it to the release) and writes the README's GIFs `docs/shots/motion_*.gif`. Single clips:
  `ZenithLancer --video clip.mp4 --replay <a replay> | --level e3_05 [--from S | --boss] [--seconds S]`.
- **Linux desktop** (or WSLg): started once from the unpacked archive.

## 5. Pictures

`README.md` shows these from `docs/shots`; `publish_release.ps1` refuses to publish while one is missing. The game
takes them itself in a hidden window of the settings' size (1600 x 900 when none is set; add `--size 1600x900` to be
sure); the times are suggestions, look for the moment that shows most. Keep each picture under about 1 MB and commit them.

| File | What | How |
|---|---|---|
| `hero.png` | a mission over the foundries of Pyra | `ZenithLancer --level e1_05 --at 45 --shot docs/shots/hero.png` |
| `workshop.png` | Quill's workshop | `ZenithLancer --screen shop --shot docs/shots/workshop.png` |
| `plan.png` | the mission plan with a fork | `ZenithLancer --screen plan --shot docs/shots/plan.png` |
| `boss.png` | a boss fight (the Foundry Warden) | `ZenithLancer --level e1_10 --at 160 --shot docs/shots/boss.png` |
| `thalassa.png` | the storm belts of Thalassa | `ZenithLancer --level e2_03 --at 40 --shot docs/shots/thalassa.png` |
| `shatter.png` | the asteroid belt of the Shatter | `ZenithLancer --level e3_01 --at 40 --shot docs/shots/shatter.png` |
| `sylvara.png` | the jungle moon Sylvara | `ZenithLancer --level e4_01 --at 40 --shot docs/shots/sylvara.png` |
| `corona.png` | the corona of Solenne | `ZenithLancer --level e5_03 --at 40 --shot docs/shots/corona.png` |
| `coop.png` | the campaign for two | F12 in a campaign for two (the picture lands in the player's folder, `shots`) |
| `radio.png` | a radio call with its speaker's portrait | `ZenithLancer --level e1_01 --at 2 --shot docs/shots/radio.png` |
| `logbook.png` | the log book and its crystals | `ZenithLancer --screen logbook --shot docs/shots/logbook.png` |

By hand on GitHub, once the repository is there: Settings > Social preview (1280 x 640, for instance a crop of
`hero.png`), the topics and the website.

## 6. The archives

- `.\Deploy\build_release.ps1` builds the Release, runs the headless tests and packs
  `dist\ZenithLancer-x.y.z-windows-x64.zip`; through WSL (Ubuntu-22.04 with GCC 12) `Deploy/build_linux.sh` makes
  `dist/ZenithLancer-x.y.z-linux-x86_64.tar.gz`. Both let the packed program check the data beside it.
- Each archive holds `ZenithLancer(.exe)`, `data/`, `assets/` (fonts, music, portraits, voice), `mods/example/`,
  `README.txt`, `CREDITS.txt` and `LICENSE`. Unpack the zip into a new folder and start it once from there.
- The unpacked size goes into `docs/STORE.md` (storage).

## 7. Publishing

1. Everything committed on master, the working tree clean.
2. `.\Deploy\publish_release.ps1`: the dry run. Every check says `ok`; read `work\publish\vx.y.z\release.md`, the
   release's text (the changelog's section, the downloads, the requirements).
3. `.\Deploy\publish_release.ps1 -Publish` (needs `gh`, logged in as reneweller-coding): makes the public repository
   `reneweller-coding/ZenithLancer` where it is missing, pushes master and the tag, and makes a DRAFT release with
   both archives and `SHA256SUMS.txt`.
4. On GitHub, Actions: the tag's `release` run builds both archives on clean machines with the tests and attaches
   only what the draft lacks; `build` runs for master. Both must be green.
5. Look at the draft (text, files, sizes), then make it public: the button on GitHub, or
   `gh release edit vx.y.z --repo reneweller-coding/ZenithLancer --draft=false`. Only then do the download links in
   `README.md` work.
6. Where wanted: the itch.io page from `docs/STORE.md`, with the same two archives.

## If something goes wrong

- **The Windows run fails to configure:** the runner image no longer has Visual Studio 18 2026, which the msvc preset
  names. Configure in the workflow with `cmake -S . -B build/msvc -A x64 -DZENITH_BIN_DIR=bin/msvc` (the newest
  Visual Studio of the image) instead of the preset.
- **The Linux run fails where MSVC builds:** GCC 12 wants the includes MSVC lets slip (`<cstddef>`, `<cstdint>`,
  `<algorithm>`); the fix goes into src on master, then a new tag.
- **A tag went out too early:** delete the draft on GitHub, then the tag there and here
  (`git push origin :refs/tags/vx.y.z`, `git tag -d vx.y.z`), fix, and publish again.
