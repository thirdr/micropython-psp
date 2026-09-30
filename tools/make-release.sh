#!/usr/bin/env bash
# Build the release zip: the EBOOT, the examples and a README, laid out to
# copy straight onto a memory stick.
#
# Usage: tools/make-release.sh [path/to/EBOOT.PBP]
#
# Writes build/release/micropython-psp-<version>.zip, containing
#   PSP/GAME/MicroPython/EBOOT.PBP
#   PSP/GAME/MicroPython/*.py              (from examples/)
#   PSP/GAME/MicroPython/README.txt        (from ports/psp/release/)
#   PSP/GAME/MicroPython/LICENSE-MicroPython.txt
# The version comes from the build (build/psp/VERSION, set in
# ports/psp/CMakeLists.txt).
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
eboot="${1:-$REPO/build/psp/EBOOT.PBP}"
version_file="$(dirname "$eboot")/VERSION"

if [ ! -f "$eboot" ]; then
    echo "no such file: $eboot (build the port first)" >&2
    exit 2
fi
if [ ! -f "$version_file" ]; then
    echo "no VERSION next to $eboot (rebuild with the current CMakeLists.txt)" >&2
    exit 2
fi
version="$(cat "$version_file")"

name="micropython-psp-$version"
stage="$REPO/build/release/$name"
game="$stage/PSP/GAME/MicroPython"
rm -rf "$stage" "$stage.zip"
mkdir -p "$game"
cp "$eboot" "$game/EBOOT.PBP"
cp "$REPO"/examples/*.py "$game/"
cp "$REPO/ports/psp/release/README.txt" "$game/README.txt"
cp "$REPO/micropython/LICENSE" "$game/LICENSE-MicroPython.txt"

# Python's zipfile, so this needs no zip tool (and adds no macOS metadata).
(cd "$stage" && python3 -m zipfile -c "$stage.zip" PSP)
echo "$stage.zip"
python3 -m zipfile -l "$stage.zip"
