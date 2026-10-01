# MicroPython for the PlayStation Portable (PSP)

Write Python scripts and run them on a PSP. This is a port of
[MicroPython](https://micropython.org) 1.28 to the PSP. It runs on a PSP with
custom firmware and in the [PPSSPP](https://www.ppsspp.org) emulator.

![The script launcher on a PSP screen: a list of seven .py files](docs/launcher.png)

Scripts can read the buttons, the analog stick, the battery, the clock and the
memory stick, print text, and draw on the screen with Pimoroni's
[PicoVector](https://github.com/pimoroni/picovector-micropython) at 60 frames
a second. There's no sound or networking yet. If you'd like to see more, star
the repo or open an issue: that decides how much further this goes.

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

The zip comes with examples: `hello`, `buttons`, `stick`, `dice`, `clock`,
`notes` and `tasks`, plus `bounce`, `sketch` and `clockface` for graphics.
They're in [`examples/`](examples) too.

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

## Graphics: `picovector` and `pspdisplay`

Scripts draw with Pimoroni's
[PicoVector](https://github.com/pimoroni/picovector-micropython), the
graphics library of Pimoroni's Badgeware badges, into `screen`, a 480×272
picovector image that `pspdisplay` puts on the PSP screen:

```python
import psp
from picovector import color, font, shape, vec2
from pspdisplay import screen, update, WIDTH, HEIGHT   # 480, 272

screen.font = font.load("fonts/sins.ppf")   # a font file next to the script
x = 0
while psp.START not in psp.pressed():
    screen.pen = color.rgb(0, 0, 0)
    screen.clear()
    screen.pen = color.rgb(255, 0, 0)
    screen.circle(vec2(x, HEIGHT // 2), 20)
    screen.text("Hello", vec2(8, 8))
    update()      # shows the frame at the next screen refresh
    x = (x + 4) % WIDTH
```

`picovector` has shapes (circles, polygons, stars, arcs, lines and strokes),
brushes and colours (RGB, HSV and OKLCH, with alpha), images (load PNG, JPEG
and GIF files, blit and scale them, sprite sheets and filters), transforms,
vector (`.af`) and pixel (`.ppf`) fonts, and tweens. On the PSP:

- `update()` waits for the screen refresh, so a drawing loop runs at up to
  60 frames a second without any other pacing. The drawing stays in place
  between frames.
- Antialiasing is off until you set `screen.antialias = image.X2` or `X4`.
- Fonts load from files: `font.load("fonts/sins.ppf")` with a folder in the
  name opens that file, relative to the script's folder. A name on its own
  (`font.load("sins")`, or `font.sins`) looks in folders such as
  `/rom/fonts`, which the PSP doesn't have. The examples come with
  `fonts/sins.ppf`, one of the Badgeware pixel fonts.
- While a script draws, `print()` doesn't reach the screen. When the script
  ends, the launcher takes the screen back with the last frame still
  showing, and the next script's `screen` starts black.
- On a PSP-1000, a fully saturated colour (such as a pure rainbow) shifting
  slowly across the whole screen flickers. That's the PSP's LCD, not the
  drawing.

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
- **No sound.**
- **Text from `print()` doesn't scroll.** It goes to the PSP's debug screen,
  and when the screen fills up it wraps back to the top.
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

The EBOOT also contains Pimoroni's PicoVector (from
[picovector-micropython](https://github.com/pimoroni/picovector-micropython)),
with the PNGdec and JPEGDEC decoders (Apache 2.0) and the QR Code generator
library (MIT) it bundles, and the pspdev
toolchain's pspsdk (BSD), newlib (mostly BSD-style) and pthread-embedded
(LGPL 2.1) libraries. Their licenses are in
[`ports/psp/release/licenses`](ports/psp/release/licenses)
and ship in the release zip. The examples' font, `fonts/sins.ppf`, is from
Pimoroni's [tufty2350](https://github.com/pimoroni/tufty2350) (MIT).
