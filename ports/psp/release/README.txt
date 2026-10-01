MicroPython for the PlayStation Portable (PSP)
==============================================

MicroPython (a small Python 3) that runs on a PSP with custom firmware,
such as ARK-4, and in the PPSSPP emulator.

Installing
----------
Copy the PSP folder from this zip onto the memory stick, merging it with
the PSP folder already there. You should end up with:

    PSP/GAME/MicroPython/EBOOT.PBP

Then pick MicroPython under Game > Memory Stick.

Running scripts
---------------
Put .py files in PSP/GAME/MicroPython/, next to EBOOT.PBP. Starting
MicroPython shows them in a list:

    Up/Down   choose a script
    X         run it
    Triangle  show its source (Up/Down scroll, Left/Right turn a page)
    O         back to the list
    HOME      quit

If there's a main.py in the folder, MicroPython runs that instead of the
list (after boot.py, if there is one). Modules in a lib/ folder can be
imported.

The scripts that come with this zip are examples: hello.py, buttons.py,
stick.py, dice.py, clock.py, notes.py and tasks.py, for graphics,
bounce.py, sketch.py and clockface.py, for sound, keys.py, and for
Wi-Fi, wifi.py. Read them, change them, or delete
them.

The psp module
--------------
    import psp
    psp.held()            buttons held now, e.g. ['CROSS', 'UP']
    psp.pressed()         buttons pressed since the last call
    psp.released()        buttons released since the last call
    psp.CROSS             button names: UP DOWN LEFT RIGHT CROSS CIRCLE
                          TRIANGLE SQUARE L R START SELECT
    psp.analog()          stick position (x, y), -127..127
    psp.set_deadzone(n)   ignore stick movement within n of centre (16)
    psp.vsync()           wait for the next screen refresh (60 Hz)
    psp.battery()         battery percent, or None
    psp.battery_minutes() minutes left, or None
    psp.charging()        True while charging
    psp.on_ac()           True on mains power
    psp.freq()            (cpu, bus) clock in MHz; psp.freq(222) sets it
    psp.emulator()        True in PPSSPP
    psp.VERSION           this release's version

Graphics
--------
Pimoroni's PicoVector (the Badgeware graphics library) draws into
pspdisplay's screen, a 480x272 picovector image:

    from picovector import color, font, vec2
    from pspdisplay import screen, update
    screen.pen = color.rgb(255, 0, 0)
    screen.circle(vec2(240, 136), 40)
    screen.font = font.load("fonts/sins.ppf")
    screen.text("Hello", vec2(8, 8))
    update()     # shows it at the next screen refresh (60 Hz)

Fonts load from files, with a folder in the name (fonts/sins.ppf, next to
the examples). While a script draws, print() doesn't reach the screen; when
it ends, the last frame stays until you press O.

Sound
-----
    import audio
    s = audio.play("music.mp3", loop=True)   # or a .wav file
    s.volume = 0.5        # also s.pause(), s.resume(), s.stop(), s.playing
    audio.volume(0.8)     # master volume
    audio.stop()          # stop everything
    out = audio.Stream(rate=22050, channels=1, bits=16)
    out.write(samples)    # samples made by the script

WAV files can be 8 or 16-bit, mono or stereo, at any sample rate. MP3s
play on the PSP's hardware decoder. Up to 8 sounds play at once, at most
2 of them MP3s; one more stops the oldest. Sounds stop when the script
ends.

Wi-Fi
-----
    import network, requests
    wlan = network.WLAN()
    wlan.profiles()          # the networks saved in Settings
    wlan.connect("Home")     # by name or number
    print(wlan.ifconfig())   # (ip, netmask, gateway, dns)
    r = requests.get("http://example.com/")
    wlan.disconnect()

Add your network in Settings > Network Settings first, and turn the Wi-Fi
switch on. socket is MicroPython's standard module (TCP, UDP, timeouts,
select.poll). requests does plain http:// only; there's no HTTPS yet.

Most standard modules are there too: os, time, json, re, random, math,
struct, collections, asyncio and more.

os.rename() can only rename within a folder: the PSP can't move a file
to another folder in one step, so os.rename() raises OSError (EXDEV, 18)
if you try. Copy the file and delete the old one instead.

License
-------
MicroPython for the PlayStation Portable (PSP) is MIT-licensed; see
LICENSE.txt. MicroPython itself is MIT-licensed too; see
LICENSE-MicroPython.txt. Pimoroni's PicoVector, with PNGdec and JPEGDEC
(Apache 2.0) and the QR Code generator library (MIT), micropython-lib's
requests (MIT) and the PSP toolchain libraries (pspsdk, newlib and
pthread-embedded), also built into EBOOT.PBP, have their own licenses, in
the licenses folder. The examples' font, fonts/sins.ppf, is from Pimoroni's
tufty2350 (MIT, licenses/tufty2350-fonts). pthread-embedded is LGPL;
its source is at https://github.com/pspdev/pthread-embedded, and this
program's source, to rebuild it, is at
https://github.com/thirdr/micropython-psp.
