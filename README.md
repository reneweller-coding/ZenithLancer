# Zenith Lancer

A vertical scrolling shooter with a workshop between missions, for PC (Windows, Linux) and the Steam Deck:
a campaign of five episodes with branching and secret missions and log crystals, an arcade mode for one or two
players, a boss rush, seven difficulties. C++20 on raylib 5.5; ships, enemies, grounds and sound effects are made
in code, the music with ACE-Step (local).

- Design, world and goods: [docs/DESIGN.md](docs/DESIGN.md)
- Data formats (missions, enemies, models, guns, stories): [docs/DATA.md](docs/DATA.md)
- For players: [docs/README-player.txt](docs/README-player.txt)

## Build

```
.\build.ps1 [-Test] [-Run]                 # Windows, MSVC (Visual Studio 18): bin\msvc\ZenithLancer.exe
cmake --preset linux; cmake --build --preset linux   # Linux (GCC 12)
.\Deploy\build_release.ps1                 # dist\ZenithLancer-<v>-windows-x64.zip and, via WSL, the Linux .tar.gz
```

## Tools

```
ZenithLancer --validate                    # every data file and reference
ZenithLancer --simall [--difficulty N] [--episode E] [--mortal] [--loadout "..."]   # autopilot balance runs
ZenithLancer --weapons                     # every gun's damage and energy a second
ZenithLancer --shot work/x.png --level e1_01 --at 60 [--theme lava] [--loadout "..."]
ZenithLancer --screen shop --shot work/shop.png
ZenithLancer --gallery work/models.png
ZenithLancer --apptest tools/apptest_campaign.txt   # with ZENITH_HOME set to a test folder
python tools/make_icon.py                  # the icon
tools/make_music.py                        # the music (AudioGenerator's venv, GPU)
```
