#!/usr/bin/env bash
# Builds ZENITH LANCER for Linux (Release, GCC 12), runs the headless tests and packs
# dist/ZenithLancer-<version>-linux-x86_64.tar.gz: the program, its data and assets (fonts, music, portraits, voices:
# all of assets/), the player's readme, the credits, the licence and, once docs/example_mod exists, the example mod as
# mods/example. Built on Ubuntu 22.04 it runs on glibc 2.35 and newer, SteamOS (the Steam Deck) included.
#
#   bash Deploy/build_linux.sh [BUILD_DIR]      (default ~/build/zenithlancer-release; nice -n 15, four jobs)
#
# The settings are the linux preset's (CMakePresets.json: Release, GCC); the CI passes the preset's own build/linux.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:-$HOME/build/zenithlancer-release}"
version="$(sed -n 's/^project( ZenithLancer VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")"

# raylib's source from the Windows build where there is one (no download), Ninja where there is one.
extra=()
[ -d "$root/build/msvc/_deps/raylib-src" ] && extra+=(-DFETCHCONTENT_SOURCE_DIR_RAYLIB="$root/build/msvc/_deps/raylib-src")
command -v ninja > /dev/null && extra+=(-G Ninja)
CC="${CC:-gcc-12}" CXX="${CXX:-g++-12}" cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release "${extra[@]}"
nice -n 15 cmake --build "$build" --parallel 4

# The headless tests: every data file and every mission flown to its end by the autopilot (no window; the app tests
# play the screens in a window and stay with the desktop builds).
nice -n 15 ctest --test-dir "$build" -R "data|missions" --output-on-failure

name="ZenithLancer-$version-linux-x86_64"
stage="$build/release/$name"
rm -rf "$build/release"
mkdir -p "$stage"
cp "$build/ZenithLancer" "$stage/ZenithLancer"
strip "$stage/ZenithLancer"
cp -r "$root/data" "$stage/data"
cp -r "$root/assets" "$stage/assets"
cp "$root/docs/README-player.txt" "$stage/README.txt"
cp "$root/docs/CREDITS.txt" "$root/LICENSE" "$stage/"
if [ -d "$root/docs/example_mod" ]; then
    mkdir -p "$stage/mods"
    cp -r "$root/docs/example_mod" "$stage/mods/example"
fi
# The packed program checks the data beside it: what the archive holds is what the player gets.
"$stage/ZenithLancer" --validate | tail -1
mkdir -p "$root/dist"
tar -C "$build/release" -czf "$root/dist/$name.tar.gz" "$name"
echo "$root/dist/$name.tar.gz"
