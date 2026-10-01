# MicroPython for the PlayStation Portable (PSP)

Write Python scripts and run them on a PSP. This is a port of
[MicroPython](https://micropython.org) 1.28 to the PSP. It runs on a PSP with
custom firmware and in the [PPSSPP](https://www.ppsspp.org) emulator.

![The script launcher on a PSP screen: a list of seven .py files](docs/launcher.png)

Scripts can read the buttons, the analog stick, the battery, the clock and the
memory stick, and print text. There's no graphics, sound or networking yet. If
you'd like to see more, star the repo or open an issue: that decides how much
further this goes.

## Installing

There's no release to download yet, so make the zip yourself:

1. Build the EBOOT (see [Building from source](#building-from-source)) and
   run `tools/make-release.sh`. It writes
   `build/release/micropython-psp-<version>.zip`.
2. Copy the `PSP` folder from the zip onto the memory stick, merging it with
   the `PSP` folder that's already there. You should end up with
   `PSP/GAME/MicroPython/EBOOT.PBP`.
3. On the PSP, open **Game → Memory Stick → MicroPython**.

It has been tested on a PSP-1000 running ARK-4, and in PPSSPP 1.20.4. Other
models and custom firmwares should work too, but haven't been tried yet.

## Running scripts

Put `.py` files in `PSP/GAME/MicroPython/`, next to `EBOOT.PBP`. When
MicroPython starts, it lists them:

| Button | Action |
| --- | --- |
| Up / Down | choose a script |
| ✕ | run it |
| △ | show its source |
| ○ | back to the list |
| HOME | quit |

If the folder has a `main.py`, MicroPython runs that instead of showing the
list (after `boot.py`, if there is one). Modules in a `lib/` folder can be
imported.

The zip comes with seven examples: `hello`, `buttons`, `stick`, `dice`,
`clock`, `notes` and `tasks`. They're in [`examples/`](examples) too.

## The `psp` module

```python
import psp

print("Press some buttons. START finishes.")
while True:
    for b in psp.pressed():
        print("pressed", b, "held:", " ".join(psp.held()))
        if b == psp.START:
            raise SystemExit
    psp.vsync()
```

| Call | Returns |
| --- | --- |
| `psp.held()` | buttons held now, for example `['CROSS', 'UP']` |
| `psp.pressed()` | buttons pressed since the last call |
| `psp.released()` | buttons released since the last call |
| `psp.UP`, `psp.CROSS`, ... | button names: `UP DOWN LEFT RIGHT CROSS CIRCLE TRIANGLE SQUARE L R START SELECT` |
| `psp.analog()` | the stick position `(x, y)`, each -127 to 127 |
| `psp.set_deadzone(n)` | ignore stick movement within `n` of the centre (default 16) |
| `psp.vsync()` | waits for the next screen refresh (60 Hz) |
| `psp.battery()` | battery percent, or `None` |
| `psp.battery_minutes()` | minutes left, or `None` |
| `psp.charging()`, `psp.on_ac()` | `True` while charging, or on mains power |
| `psp.freq()` | the clock as `(cpu, bus)` in MHz; `psp.freq(222)` sets it |
| `psp.emulator()` | `True` in PPSSPP |
| `psp.VERSION` | the port's version |

## What works and what doesn't

**Works:**
- Most of MicroPython's standard library: `os`, `time`, `json`, `re`,
  `random`, `math`, `struct`, `collections`, `asyncio`, `deflate`, `hashlib`
  and more. MicroPython's own test suite passes (793 tests).
- Files on the memory stick, with `open()` and `os`. Scripts run with their
  own folder as the working directory.
- `time` reads the PSP's real-time clock and time zone.
- About 12 MB of memory for Python, sized to fit a PSP-1000.

**Doesn't work yet:**
- **No graphics or sound.** Output is text on the PSP's debug screen. When the
  screen fills up, text wraps back to the top instead of scrolling.
- **No interactive prompt (REPL).** You write scripts on a computer and run
  them from the launcher.
- **No Wi-Fi, USB or threads.**
- **Floats are single precision** (about 7 significant digits).
- **`os.rename` can't move a file to another folder.** The PSP can only rename
  within a folder, so a move raises `OSError` (`EXDEV`, 18). Copy the file
  and delete the old one instead.

## Building from source

You need the [pspdev](https://github.com/pspdev/pspdev) toolchain, CMake,
Python 3 and a host C compiler (for `mpy-cross`).

```sh
git clone --recurse-submodules https://github.com/thirdr/micropython-psp.git
cd micropython-psp
psp-cmake -S ports/psp -B build/psp -DCMAKE_BUILD_TYPE=Release
cmake --build build/psp -j8
```

That writes `build/psp/EBOOT.PBP`. `tools/make-release.sh` packs it into the
release zip, in `build/release/`.

The port lives in [`ports/psp`](ports/psp) and builds against the
[`micropython`](micropython) submodule, which isn't modified.

## Testing

The tests run in PPSSPPHeadless, PPSSPP's command-line build, which you
build from PPSSPP's source. Set `PPSSPP_HEADLESS` to its path (the default is
`../ppsspp/build-headless/PPSSPPHeadless`). `tools/run-ppsspp.sh` runs an EBOOT, shows its
output in the terminal and saves a screenshot:

```sh
tools/run-ppsspp.sh --files tests/fs build/psp/EBOOT.PBP 30
tools/run-mp-tests.sh   # MicroPython's own test suite
```

The folders in [`tests/`](tests) are script sets that each run as a PSP
folder. CI ([`.github/workflows/build.yml`](.github/workflows/build.yml))
builds the EBOOT, runs every test headless, and makes the release zip.

## License

MIT; see [`LICENSE`](LICENSE). MicroPython itself is MIT-licensed too; see
[`micropython/LICENSE`](micropython/LICENSE).

The EBOOT also contains the pspdev toolchain's pspsdk (BSD), newlib (mostly
BSD-style) and pthread-embedded (LGPL 2.1) libraries. Their licenses are in
[`ports/psp/release/licenses`](ports/psp/release/licenses)
and ship in the release zip.
